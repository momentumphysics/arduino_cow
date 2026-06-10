#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "MAX30105.h"
#include <WiFiManager.h> 
#include <ArduinoOTA.h>

WebServer server(80);

#define ONE_WIRE_BUS 4
Adafruit_MPU6050 mpu;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
MAX30105 max30102;

// Variabel Global Data Sensor
float ax = 0, ay = 0, az = 0;
float suhuDS = 0;
uint32_t irValue = 0, redValue = 0;

unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 200; 

// Fungsi helper map() untuk tipe data float
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// API Data JSON
void handleDataAPI() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET");
  
  String json = "{";
  json += "\"ax\":" + String(ax, 2) + ",";
  json += "\"ay\":" + String(ay, 2) + ",";
  json += "\"az\":" + String(az, 2) + ",";
  json += "\"suhu\":" + String(suhuDS, 2) + ",";
  json += "\"ir\":" + String(irValue) + ",";
  json += "\"red\":" + String(redValue);
  json += "}";
  
  server.send(200, "application/json", json);
}

// Fungsi untuk Inisialisasi Fitur OTA
void setupOTA() {
  // Anda bisa menambahkan password jika ingin OTA lebih aman:
   ArduinoOTA.setPassword("admin123");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("[-] Memulai proses OTA update: " + type);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[+] OTA Update Selesai!");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[~] Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[!] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Gagal Autentikasi");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Gagal Memulai (Begin)");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Gagal Koneksi");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Gagal Menerima Data");
    else if (error == OTA_END_ERROR) Serial.println("Gagal Mengakhiri (End)");
  });

  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // Inisialisasi Attenuation ADC untuk pembacaan tegangan up to 3.3V
  analogSetAttenuation(ADC_11db); 

  if (mpu.begin()) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }
  ds18b20.begin();
  if (max30102.begin(Wire, I2C_SPEED_FAST)) {
    max30102.setup(60, 4, 2, 100, 411, 4096);
  }

  // --- KONFIGURASI WIFIMANAGER ---
  WiFiManager wm;

  // wm.resetSettings(); // Buka komentar ini jika ingin reset WiFi tersimpan saat testing

  // Membuka Hotspot bernama "ESP32_Config_AP" tanpa password jika tidak terkoneksi ke WiFi lama.
  bool res = wm.autoConnect("ESP32_Config_AP");

  if(!res) {
    Serial.println("[!] Gagal terhubung ke WiFi dan waktu konfigurasi habis.");
    ESP.restart(); 
  } 
  
  Serial.println("\n[+] WiFi Terhubung Sukses!");
  Serial.print("[!] IP ESP32 Anda: ");
  Serial.println(WiFi.localIP());

  // 2. Jalankan fungsi inisialisasi OTA setelah WiFi tersambung
  setupOTA();

  // Jalankan server API data
  server.on("/api/data", handleDataAPI);
  server.begin();
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle(); // <-- 3. Selalu handle request OTA di setiap perulangan

  if (millis() - lastSensorRead >= sensorInterval) {
    lastSensorRead = millis();

    // 1. Membaca Sensor MPU6050
    sensors_event_t a, g, temp_mpu;
    mpu.getEvent(&a, &g, &temp_mpu);
    ax = a.acceleration.x;
    ay = a.acceleration.y;
    az = a.acceleration.z;

    // 2. Membaca Sensor DS18B20
    ds18b20.requestTemperatures();
    float t = ds18b20.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C) suhuDS = t;

    // 3. Membaca Sensor MAX30102
    irValue = max30102.getIR();
    redValue = max30102.getRed();
  }
}