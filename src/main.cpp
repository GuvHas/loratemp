#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include "SSD1306.h"
#include <DHT.h>

// ==========================================
//              USER CONFIGURATION
// ==========================================
const String NodeId = "GarageTemp"; 
const int SLEEP_MINUTES = 5;         

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

// DHT Sensor
#define DHTPIN 13
#define DHTTYPE DHT22

// ==========================================
//             GLOBAL OBJECTS
// ==========================================
SSD1306 display(0x3C, 21, 22);
DHT dht(DHTPIN, DHTTYPE);

// Store boot count in RTC memory
RTC_DATA_ATTR int bootCount = 0;

// ==========================================
//           HELPER FUNCTIONS
// ==========================================

float getBatteryVoltage() {
  // 1. Read the Analog Value (0 - 4095)
  int reading = analogRead(BAT_PIN);
  
  // 2. Calculate Voltage
  // 3.3V reference / 4095 steps * 2 (Voltage Divider)
  float voltage = ((float)reading / 4095.0) * 3.3 * 2.0;
  
  // Optional: Small calibration correction (e.g. +0.1) if reading is low
  // voltage += 0.1; 

  return voltage;
}

void goToSleep() {
  Serial.println("Going to sleep...");
  LoRa.end();
  display.displayOff();
  digitalWrite(LED_PIN, LOW);
  
  uint64_t sleepTime = SLEEP_MINUTES * 60 * 1000000ULL;
  esp_sleep_enable_timer_wakeup(sleepTime);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  
  // Standard analog read setup
  analogReadResolution(12);

  bootCount++;
  Serial.println("\n\n--- Boot #" + String(bootCount) + " ---");

  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "Reading Sensor...");
  display.display();

  // Init DHT
  dht.begin();
  delay(2000); // Allow sensor and voltage to stabilize

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  float v = getBatteryVoltage(); // <--- Get Battery Voltage

  // Basic error check for DHT
  if (isnan(t) || isnan(h)) {
    Serial.println("DHT Read Failed!");
    // We send 0.0 for temp/hum so we can still see the battery report in HA
    t = 0.0;
    h = 0.0;
  }

  // Init LoRa
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  LoRa.setPins(SS_PIN, RST_PIN, DI0_PIN);
  
  if (!LoRa.begin(BAND)) {
    Serial.println("LoRa Init Failed!");
    delay(1000);
    goToSleep();
  }

  // Build JSON: {"id":"GarageTemp","t":22.5,"h":50.2,"v":4.12}
  String msg = "{\"id\":\"" + NodeId + "\",\"t\":" + String(t) + ",\"h\":" + String(h) + ",\"v\":" + String(v) + "}";
  
  Serial.print("Sending: ");
  Serial.println(msg);

  digitalWrite(LED_PIN, HIGH);
  LoRa.beginPacket();
  LoRa.print(msg);
  LoRa.endPacket();
  
  // Visual Feedback
  display.clear();
  display.drawString(0, 0, "Sent Data:");
  display.drawString(0, 15, "T: " + String(t) + "C");
  display.drawString(0, 30, "H: " + String(h) + "%");
  display.drawString(0, 45, "Bat: " + String(v) + "V"); // <--- Show on screen
  display.display();
  
  delay(1000); 
  goToSleep();
}

void loop() {
  // Never reached
}