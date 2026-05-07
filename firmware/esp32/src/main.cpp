/*
 * ac-iot-ufma — Firmware ESP32
 * 
 * Módulo principal: leitura de sensores (DHT22 + PIR),
 * publicação MQTT e recepção de comandos IR para o ar-condicionado.
 * 
 * Este é um esqueleto inicial para testes de conectividade.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

// ── Configurações Wi-Fi ──────────────────────────
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// ── Configurações MQTT ───────────────────────────
const char* MQTT_BROKER = "host.docker.internal"; // Para Wokwi → Docker
const int   MQTT_PORT   = 1883;
const char* MQTT_CLIENT = "esp32_sala01";

// Tópicos MQTT
const char* TOPIC_SENSOR   = "ac-iot/sala01/sensores";
const char* TOPIC_COMANDO  = "ac-iot/sala01/comando";
const char* TOPIC_STATUS   = "ac-iot/sala01/status";

// ── Pinos ────────────────────────────────────────
#define DHT_PIN    4
#define PIR_PIN    15
#define IR_LED_PIN 26
#define DHT_TYPE   DHT22

// ── Objetos ──────────────────────────────────────
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);
DHT          dht(DHT_PIN, DHT_TYPE);

// ── Intervalo de publicação (ms) ─────────────────
unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 5000;

// ── Protótipos ───────────────────────────────────
void setupWiFi();
void setupMQTT();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void publishSensorData();

void setup() {
  Serial.begin(115200);
  Serial.println("\n[ac-iot-ufma] Inicializando...");

  pinMode(PIR_PIN, INPUT);
  dht.begin();

  setupWiFi();
  setupMQTT();

  Serial.println("[ac-iot-ufma] Setup concluído!");
}

void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  if (millis() - lastPublish >= PUBLISH_INTERVAL) {
    publishSensorData();
    lastPublish = millis();
  }
}

// ── Implementações ───────────────────────────────

void setupWiFi() {
  Serial.printf("[WiFi] Conectando a %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
}

void setupMQTT() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Conectando ao broker...");
    if (mqttClient.connect(MQTT_CLIENT)) {
      Serial.println(" OK!");
      mqttClient.subscribe(TOPIC_COMANDO);
      mqttClient.publish(TOPIC_STATUS, "{\"status\":\"online\"}");
    } else {
      Serial.printf(" FALHOU (rc=%d). Tentando em 5s...\n", mqttClient.state());
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.printf("[MQTT] Mensagem recebida [%s]: %s\n", topic, message.c_str());

  // TODO: Interpretar comando JSON e acionar IR
}

void publishSensorData() {
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();
  int   presence    = digitalRead(PIR_PIN);

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("[Sensor] Erro na leitura do DHT22!");
    return;
  }

  JsonDocument doc;
  doc["sala"]        = "sala01";
  doc["temperatura"] = temperature;
  doc["humidade"]    = humidity;
  doc["presenca"]    = (presence == HIGH);
  doc["timestamp"]   = millis();

  char buffer[256];
  serializeJson(doc, buffer);

  mqttClient.publish(TOPIC_SENSOR, buffer);
  Serial.printf("[MQTT] Publicado: %s\n", buffer);
}
