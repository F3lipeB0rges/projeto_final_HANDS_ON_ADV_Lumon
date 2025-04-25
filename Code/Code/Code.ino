#include <WiFi.h>
#include <MPU6050.h>
#include <DHT.h>
#include <Wire.h>
#include <Firebase_ESP_Client.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define API_KEY "AIzaSyBrJ_sECaD7OWfJ_sgk2MbDsmrhz29FmR8"

#define DATABASE_URL "https://sleepguard-82799-default-rtdb.firebaseio.com/"

FirebaseData fdbo;
FirebaseAuth auth;
FirebaseConfig config;

String nome = "";
bool signupOK = false;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600, 60000);

MPU6050 mpu;

#define SDA_PIN 22
#define SCL_PIN 23
#define BUZZER_PIN 15
#define PIR_PIN 21
#define GREEN_PIN 18
#define RED_PIN 19
#define DHT_PIN 4
#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

const char* ssid = "CIT_Alunos";
const char* password = "alunos@2024";

unsigned long lastMotionTime = 0;
unsigned long motionDelay = 3000;
bool motionDetected = false;
int movimentoContador = 0;
const int MOVIMENTOS_MIN = 5;

unsigned long ultimoEnvio = 0;
const unsigned long intervaloEnvio = 60000;

int16_t base_ax = 0, base_ay = 0, base_az = 0;
int limite_x = 0;
int limite_y = 0;
int limite_z = 0;
bool baseDefinida = false;
int temp_min = 18;
int temp_max = 30;
int umidade_min = 30;
int umidade_max = 70;

float queda_padrao[5][3] = {
    {17100.0, -344.0, -3960.0},  
    {17094.0, -365.0, -3770.0},  
    {16888.0, -386.0, -3580.0}, 
    {16782.0, -407.0, -3390.0},  
    {16676.0, -428.0, -3200.0} 
};

float sequencia_medida[5][3]; 
int indiceSequencia = 0;

float calcularDTW(float padrao[5][3], float sequencia[5][3]) {
    float soma_distancia = 0.0;
    for (int i = 0; i < 5; i++) {
        float dist = 0.0;
        for (int j = 0; j < 3; j++) {
            dist += pow(padrao[i][j] - sequencia[i][j], 2);
        }
        soma_distancia += sqrt(dist);
    }
    return soma_distancia;
}

void buscarConfiguracoesFirebase() {
    Firebase.RTDB.getInt(&fdbo, "/config/limite_x");
    limite_x = fdbo.intData();
    Firebase.RTDB.getInt(&fdbo, "/config/limite_y");
    limite_y = fdbo.intData();
    Firebase.RTDB.getInt(&fdbo, "/config/limite_z");
    limite_z = fdbo.intData();
    Firebase.RTDB.getInt(&fdbo, "/config/temp_min");
    temp_min = fdbo.intData();
    Firebase.RTDB.getInt(&fdbo, "/config/temp_max");
    temp_max = fdbo.intData();
    Firebase.RTDB.getInt(&fdbo, "/config/umidade_min");
    umidade_min = fdbo.intData();
    Firebase.RTDB.getInt(&fdbo, "/config/umidade_max");
    umidade_max = fdbo.intData();
}

void compararComQuedaPadrao() {
    float limiarQueda = 500.0;

    int contador = 0;

    for (int i = 0; i < 5; i++) {
        float diff_x = abs(sequencia_medida[i][0] - queda_padrao[i][0]);
        float diff_y = abs(sequencia_medida[i][1] - queda_padrao[i][1]);
        float diff_z = abs(sequencia_medida[i][2] - queda_padrao[i][2]);

        if (diff_x <= limiarQueda && diff_y <= limiarQueda && diff_z <= limiarQueda) {
            contador++;
        }
    }

    if (contador >= 3) {
        enviarAlerta("Possível queda detectada!");
    }
}

void enviarAlerta(String mensagem) {
    timeClient.update();
    String horario = timeClient.getFormattedTime();
    String alertaID = String(millis());
    String caminho = "/alertas/" + alertaID;
    FirebaseJson json;
    Serial.println(String(mensagem)+" "+String(horario));
    json.set("mensagem", mensagem);
    json.set("horario", horario);
    Firebase.RTDB.setJSON(&fdbo, caminho.c_str(), &json);
}

void enviarVariaveisAmbiente() {
    timeClient.update();
    String horario = timeClient.getFormattedTime();
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    float temperatura = dht.readTemperature();
    float umidade = dht.readHumidity();
    bool movimento = digitalRead(PIR_PIN);
    
    String caminho = "/variaveisAmbiente";
    FirebaseJson json;
    json.set("horario", horario);
    json.set("posicao/x", ax/16384.0);
    json.set("posicao/y", ay/16384.0);
    json.set("posicao/z", az/16384.0);
    json.set("temperatura", isnan(temperatura) ? 20 : temperatura);
    json.set("umidade", isnan(umidade) ? 50 : umidade);
    json.set("movimento", movimento);
    bool sucesso = Firebase.RTDB.setJSON(&fdbo, caminho.c_str(), &json);
    if (!sucesso) {
      Serial.println("Falha ao enviar dados:");
      Serial.println(fdbo.errorReason());
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nWiFi conectado!");
    
    Wire.begin(SDA_PIN, SCL_PIN);
    mpu.initialize();
    dht.begin();
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(RED_PIN, OUTPUT);
    pinMode(GREEN_PIN, OUTPUT);
    pinMode(PIR_PIN, INPUT);
    
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;

    if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Signup OK!");
    signupOK = true;
    } else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
    }

    config.token_status_callback = tokenStatusCallback; 
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    
    timeClient.begin();
    buscarConfiguracoesFirebase();
    mpu.getAcceleration(&base_ax, &base_ay, &base_az);
    baseDefinida = true;
    Serial.println("Base definida:");
    Serial.print("X: "); Serial.println(base_ax);
    Serial.print("Y: "); Serial.println(base_ay);
    Serial.print("Z: "); Serial.println(base_az);
    enviarVariaveisAmbiente();
}

void loop() {
    static bool movimentoAnterior = false;

    int pirState = digitalRead(PIR_PIN);
    digitalWrite(GREEN_PIN, HIGH);
    digitalWrite(RED_PIN, LOW);

    if (pirState == HIGH) {
        if (millis() - lastMotionTime < 10000) {
            movimentoContador++;
        } else {
            movimentoContador = 1; 
        }
        lastMotionTime = millis(); 
    }
    if (movimentoContador >= MOVIMENTOS_MIN && !motionDetected) {
        motionDetected = true;
        digitalWrite(GREEN_PIN, LOW);
        digitalWrite(RED_PIN, HIGH);
        tone(BUZZER_PIN, 1000, 200);
        enviarAlerta("Movimento incomum detectado!");
        enviarVariaveisAmbiente();
    }
    if (millis() - lastMotionTime > 10000) {
        movimentoContador = 0;
        motionDetected = false;
    }

    movimentoAnterior = (pirState == HIGH);
    if (millis() - lastMotionTime > 10000) {
        digitalWrite(GREEN_PIN, LOW);
        digitalWrite(RED_PIN, HIGH);
        tone(BUZZER_PIN, 1000, 200);
        motionDetected = false;
        tone(BUZZER_PIN, 500, 500);
    }
    
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    sequencia_medida[indiceSequencia][0] = ax;
    sequencia_medida[indiceSequencia][1] = ay;
    sequencia_medida[indiceSequencia][2] = az;
    indiceSequencia++;
    if (indiceSequencia >= 5) {
        indiceSequencia = 0;
    }
  
    compararComQuedaPadrao();

    if (baseDefinida && (
    abs(ax - base_ax) > limite_x ||
    abs(ay - base_ay) > limite_y ||
    abs(az - base_az) > limite_z)) {
        digitalWrite(GREEN_PIN, LOW);
        digitalWrite(RED_PIN, HIGH);
        tone(BUZZER_PIN, 2000, 500);
        enviarAlerta("Movimento excessivo detectado! X=" + String(ax/16384.0) + ",Y=" + String(ay/16384.0) + ",Z=" + String(az/16384.0));
        enviarVariaveisAmbiente();
    }

    float temperatura = dht.readTemperature();
    float umidade = dht.readHumidity();
    if (!isnan(temperatura) && (temperatura < temp_min || temperatura > temp_max)) {
        enviarAlerta("Temperatura fora da faixa aceitável! "+String(temperatura)+" Graus");
        enviarVariaveisAmbiente();
    }
    if (!isnan(umidade) && (umidade < umidade_min || umidade > umidade_max)) {
        enviarAlerta("Umidade fora da faixa aceitável! "+String(umidade)+" Umidade");
        enviarVariaveisAmbiente();
    }
    
    if (millis() - ultimoEnvio > intervaloEnvio) {
        enviarVariaveisAmbiente();
        ultimoEnvio = millis();
    }
    delay(1000);
}
