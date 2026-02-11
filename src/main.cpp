#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include "SSD1306.h"
#include <DHT.h>
#include <WiFi.h>
#include <esp_bt.h>

// ==========================================
//              USER CONFIGURATION
// ==========================================
const char* NodeId = "GarageTemp";
const int SLEEP_MINUTES = 5;
const int DISPLAY_SECONDS = 2;       // How long to show data on OLED before sleep
const float LOW_BAT_VOLTAGE = 3.3f;  // Voltage threshold for low battery warning
const int LORA_SF = 9;               // LoRa spreading factor (7-12, higher = more range)

// Power saving options
const int LORA_TX_POWER = 14;        // TX power in dBm (2-20, lower = less range but saves battery)
const int DISPLAY_EVERY_N = 12;      // Show OLED every Nth boot (12 = ~1hr at 5min sleep). 1 = always
const int CPU_MHZ = 80;              // CPU frequency (80 is plenty for sensor work, default 240)

// ==========================================
//           HARDWARE PINS (TTGO V1.6)
// ==========================================
#define SCK_PIN  5
#define MISO_PIN 19
#define MOSI_PIN 27
#define SS_PIN   18
#define RST_PIN  23 
#define DI0_PIN  26
#define BAND     868E6 
#define LED_PIN  25
#define BAT_PIN  35  // GPIO35 is usually connected to the battery divider

// OLED Display (I2C)
#define SDA_PIN  21
#define SCL_PIN  22

// DHT Sensor
#define DHTPIN   13
#define DHTTYPE  DHT22
#define DHT_MAX_RETRIES 3

// ==========================================
//             GLOBAL OBJECTS
// ==========================================
SSD1306 display(0x3C, SDA_PIN, SCL_PIN);
DHT dht(DHTPIN, DHTTYPE);

// Store boot count in RTC memory
RTC_DATA_ATTR int bootCount = 0;

// ==========================================
//           HELPER FUNCTIONS
// ==========================================

float getBatteryVoltage() {
  // Average multiple ADC samples to reduce noise
  const int samples = 10;
  long total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(BAT_PIN);
    delay(2);
  }
  float reading = (float)total / samples;

  // 3.3V reference / 4095 steps * 2 (voltage divider ratio)
  float voltage = (reading / 4095.0f) * 3.3f * 2.0f;

  return voltage;
}

void goToSleep() {
  Serial.println("Going to sleep...");
  Serial.flush(); // Ensure serial output completes before sleep
  LoRa.end();
  display.displayOff();
  digitalWrite(LED_PIN, LOW);
  
  uint64_t sleepTime = (uint64_t)SLEEP_MINUTES * 60ULL * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepTime);
  esp_deep_sleep_start();
}

void setup() {
  // --- Power savings: disable unused radios and lower CPU ---
  setCpuFrequencyMhz(CPU_MHZ);
  WiFi.mode(WIFI_OFF);
  esp_bt_controller_disable();

  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // Standard analog read setup
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_PIN, ADC_11db);

  bootCount++;
  Serial.println("\n\n--- Boot #" + String(bootCount) + " ---");

  // Only power up the OLED on every Nth boot, first boot, or errors (checked later)
  bool showDisplay = (bootCount == 1) || (bootCount % DISPLAY_EVERY_N == 0);

  if (showDisplay) {
    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 0, "Reading Sensor...");
    display.display();
  }

  // Init DHT
  dht.begin();
  delay(2000); // Allow sensor and voltage to stabilize

  // Retry DHT reads — the sensor often fails on the first attempt after deep sleep
  float t = NAN;
  float h = NAN;
  for (int attempt = 0; attempt < DHT_MAX_RETRIES; attempt++) {
    t = dht.readTemperature();
    h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) break;
    Serial.println("DHT read attempt " + String(attempt + 1) + " failed, retrying...");
    delay(2000);
  }

  bool dhtOk = !isnan(t) && !isnan(h);
  if (!dhtOk) {
    Serial.println("DHT Read Failed after " + String(DHT_MAX_RETRIES) + " attempts!");
  }

  float v = getBatteryVoltage();

  // Init LoRa
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  LoRa.setPins(SS_PIN, RST_PIN, DI0_PIN);
  
  if (!LoRa.begin(BAND)) {
    Serial.println("LoRa Init Failed!");
    delay(1000);
    goToSleep();
  }
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setTxPower(LORA_TX_POWER);
  LoRa.enableCrc();

  // Build JSON payload using snprintf to avoid String heap fragmentation
  bool lowBat = v < LOW_BAT_VOLTAGE;
  char msg[160];
  if (dhtOk) {
    snprintf(msg, sizeof(msg),
             "{\"id\":\"%s\",\"t\":%.1f,\"h\":%.1f,\"v\":%.2f,\"boot\":%d%s}",
             NodeId, t, h, v, bootCount, lowBat ? ",\"lb\":1" : "");
  } else {
    snprintf(msg, sizeof(msg),
             "{\"id\":\"%s\",\"v\":%.2f,\"boot\":%d,\"err\":\"dht\"%s}",
             NodeId, v, bootCount, lowBat ? ",\"lb\":1" : "");
  }

  Serial.print("Sending: ");
  Serial.println(msg);

  digitalWrite(LED_PIN, HIGH);
  LoRa.beginPacket();
  LoRa.print(msg);
  int loraResult = LoRa.endPacket();
  if (loraResult == 0) {
    Serial.println("LoRa TX failed, retrying...");
    delay(100);
    LoRa.beginPacket();
    LoRa.print(msg);
    loraResult = LoRa.endPacket();
    if (loraResult == 0) {
      Serial.println("LoRa TX retry also failed!");
    }
  }
  digitalWrite(LED_PIN, LOW); // LED off immediately after TX

  // Show display on scheduled boots, or force it on for any error condition
  bool hasError = !dhtOk || !loraResult || lowBat;
  if (!showDisplay && hasError) {
    showDisplay = true;
    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
  }

  if (showDisplay) {
    display.clear();
    display.drawString(0, 0, loraResult ? "Sent OK:" : "TX FAILED:");
    if (dhtOk) {
      display.drawString(0, 15, "T: " + String(t, 1) + " \xb0" + "C");
      display.drawString(0, 30, "H: " + String(h, 1) + " %");
    } else {
      display.drawString(0, 15, "DHT: FAILED");
    }
    display.drawString(0, 45, "Bat: " + String(v, 2) + "V" + (lowBat ? " LOW!" : ""));
    display.display();
    delay(DISPLAY_SECONDS * 1000);
  }

  goToSleep();
}

void loop() {
  // Never reached
}