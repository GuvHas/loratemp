#include <Arduino.h>
#include <WiFi.h>
#include <esp_bt.h>

#include "hal_esp32.h"
#include "orchestrator.h"

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

// Store counters in RTC memory for backend gap detection across deep sleep cycles
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR uint32_t txCount = 0;

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

  DhtSensor sensor(DHTPIN, DHTTYPE);
  LoRaRadioAdapter radio(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN, RST_PIN, DI0_PIN, BAND, LORA_SF,
                          LORA_TX_POWER, LED_PIN);
  Ssd1306Display display(0x3C, SDA_PIN, SCL_PIN);
  AdcPower power(BAT_PIN);
  Esp32Clock clock;

  NodeConfig cfg{
      NodeId,             // nodeId
      DHT_MAX_RETRIES,    // dhtMaxRetries
      2000,               // dhtRetryDelayMs
      2000,               // dhtSettleDelayMs
      2,                  // loraMaxRetries (1 initial attempt + 1 retry, matching original)
      100,                // loraRetryDelayMs
      LOW_BAT_VOLTAGE,    // lowBatVoltage
      SLEEP_MINUTES,      // sleepMinutes
      DISPLAY_EVERY_N,    // displayEveryN
      DISPLAY_SECONDS,    // displaySeconds
  };

  NodeOutcome outcome = runNode(cfg, bootCount, txCount, sensor, radio, display, power, clock);

  if (!outcome.loraOk) {
    Serial.println(outcome.payload[0] == '\0' ? "LoRa Init Failed!" : "LoRa TX failed after retries!");
  }
  if (!outcome.dhtOk) {
    Serial.println("DHT Read Failed after retries!");
  }
  if (outcome.payload[0] != '\0') {
    Serial.print("Sending: ");
    Serial.println(outcome.payload);
  }
  // runNode() always ends in clock.deepSleep(), which never returns on real hardware.
}

void loop() {
  // Never reached
}
