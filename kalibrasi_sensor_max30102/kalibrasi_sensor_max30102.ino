#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "MAX30105.h"
#include <WiFiManager.h> 
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

WebServer server(80);
MAX30105 max30102;

#define MAX_BUFFER_SIZE 200
uint32_t irLog[MAX_BUFFER_SIZE];
uint32_t redLog[MAX_BUFFER_SIZE];
volatile int logCount = 0;

SemaphoreHandle_t bufferMutex;
TaskHandle_t SensorTaskHandle;

// API Endpoint (Berjalan di Core 1)
void handleDataAPI() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  // Buat buffer lokal di dalam fungsi agar kita bisa lepas Mutex secepat mungkin
  uint32_t localIR[MAX_BUFFER_SIZE];
  uint32_t localRed[MAX_BUFFER_SIZE];
  int currentCount = 0;

  // AMBIL MUTEX: Hanya untuk menyalin data ke memori lokal (Sangat Cepat! < 1ms)
  if (xSemaphoreTake(bufferMutex, portMAX_DELAY) == pdTRUE) {
    currentCount = logCount; 
    for (int i = 0; i < currentCount; i++) {
      localIR[i] = irLog[i];
      localRed[i] = redLog[i];
    }
    logCount = 0; // Reset counter
    xSemaphoreGive(bufferMutex); // LANGSUNG LEPASKAN MUTEX
  }

  // Menyusun JSON sekarang menggunakan array lokal, Core 0 tidak akan terganggu
  String json = "{\"samples\":[";
  for (int i = 0; i < currentCount; i++) {
    json += "{\"ir\":" + String(localIR[i]) + ",\"red\":" + String(localRed[i]) + "}";
    if (i < currentCount - 1) json += ",";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

// Fungsi Pembacaan Sensor (Berjalan di Core 0)
void readSensorTask(void * pvParameters) {
  for(;;) {
    max30102.check();
    while (max30102.available()) {
      
      // Tunggu hingga Mutex tersedia (pasti langsung tersedia karena Core 1 melepasnya dengan cepat)
      if (xSemaphoreTake(bufferMutex, portMAX_DELAY) == pdTRUE) { 
        if (logCount < MAX_BUFFER_SIZE) {
          redLog[logCount] = max30102.getRed();
          irLog[logCount] = max30102.getIR();
          logCount++;
          max30102.nextSample(); // Hanya bergeser jika data sukses direkam
        } else {
          max30102.nextSample(); // Amankan jika buffer penuh agar tidak hang
        }
        xSemaphoreGive(bufferMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1)); 
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  
  if (max30102.begin(Wire, I2C_SPEED_FAST)) {
    // KONFIGURASI BARU: 
    // Parameter kedua = 1 (Averaging dimatikan agar sampling rate murni)
    // Parameter keempat = 50 (Sampling Rate set ke 50 Hz)
    max30102.setup(60, 1, 2, 50, 411, 4096);
  }

  bufferMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(
    readSensorTask, "SensorTask", 4096, NULL, 3, &SensorTaskHandle, 0
  );

  WiFiManager wm;
  wm.autoConnect("ESP32_Config_AP");
  MDNS.begin("max30102");
  
  server.on("/api/data", handleDataAPI);
  server.begin();
  ArduinoOTA.setPassword("admin123");
  ArduinoOTA.begin();
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  delay(2); 
}