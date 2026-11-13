#include <Arduino.h>
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_task_wdt.h>

#include "hal_esp32.h"
#include "orchestrator.h"

// ==========================================
//              USER CONFIGURATION
// ==========================================
const char* NodeId = "GarageTemp";
const int SLEEP_MINUTES = 5;
const int DISPLAY_SECONDS = 2;       // How long to show data on OLED before sleep
// Voltage threshold for the low battery warning (lb). The old, uncalibrated
// reading was about 0.25 V low (3.93 V vs 4.18 V calibrated on the same
// battery), so the old 3.3 V threshold really fired around 3.5 V; 3.5 V keeps
// the warning at the same real battery level.
const float LOW_BAT_VOLTAGE = 3.5f;
// Below this the node still sends (with lb:1), then sleeps CRITICAL_SLEEP_MINUTES
// to protect a nearly empty LiPo from over-discharge.
const float CRITICAL_BAT_VOLTAGE = 3.3f;
const int CRITICAL_SLEEP_MINUTES = 60;
// Each sleep is offset by a random amount up to +/- this, so nodes powered
// up together do not keep transmitting at the same moment.
const int SLEEP_JITTER_MS = 5000;
const int LORA_SF = 9;               // LoRa spreading factor (7-12, higher = more range)

// Power saving options
const int LORA_TX_POWER = 14;        // TX power in dBm (2-20, lower = less range but saves battery)
const int DISPLAY_EVERY_N = 12;      // Show OLED every Nth boot (12 = ~1hr at 5min sleep). 1 = always
const int CPU_MHZ = 80;              // CPU frequency (80 is plenty for sensor work, default 240)
// Spend the DHT settle time and the OLED hold in light sleep (~1 mA) instead
// of a busy delay (~25 mA at 80 MHz). Set false to rule it out if the sensor
// or display misbehaves on a board.
const bool LIGHT_SLEEP_WAITS = true;

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
#define DHT_MAX_RETRIES 2  // 1 attempt + 1 retry; each retry waits 2 s for the DHT22

// Upper bound on one wake. A normal wake takes a few seconds (the worst
// healthy case, with every DHT retry, both LoRa attempts timing out and the
// display on, is about 10 s). If anything hangs past this, the task
// watchdog panics and the chip restarts instead of staying awake until the
// battery is empty. A restart re-initialises RTC memory, so the next packet
// carries boot: 1 and the gateway treats it as a cold boot.
#define WAKE_WATCHDOG_S 30

// Store counters in RTC memory for backend gap detection across deep sleep cycles
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR uint32_t txCount = 0;
// Resets to false only on a real power loss (flash/battery swap), same as
// the counters above. Tracks whether the gateway has actually received this
// node's firmware version yet -- see runNode()'s doc comment.
RTC_DATA_ATTR bool versionReported = false;

// With WiFi and Bluetooth off, the hardware RNG has no RF entropy source and
// esp_random() is only pseudo-random. Mixing in this chip's MAC guarantees
// that two nodes woken in step still get different sleep offsets and drift
// apart, which is all the jitter needs.
static uint32_t wakeRandom() {
  uint64_t mac = ESP.getEfuseMac();
  return esp_random() ^ static_cast<uint32_t>(mac) ^ static_cast<uint32_t>(mac >> 32) ^
         (bootCount * 2654435761u);
}

// Arduino core 3.x starts the task watchdog before setup(), so it is
// reconfigured rather than initialised (same approach as loragateway).
static void startWakeWatchdog() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wdtConfig = {
      .timeout_ms = WAKE_WATCHDOG_S * 1000,
      .idle_core_mask = (1 << 0),
      .trigger_panic = true,
  };
  if (esp_task_wdt_reconfigure(&wdtConfig) == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_init(&wdtConfig);
  }
#else
  esp_task_wdt_init(WAKE_WATCHDOG_S, true);
#endif
  // Subscribe this task and never feed it: the whole wake must finish
  // inside the timeout and end in deep sleep, which stops the watchdog.
  esp_task_wdt_add(NULL);
}

void setup() {
  startWakeWatchdog();

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
  Esp32Clock clock(LIGHT_SLEEP_WAITS);
  SerialLogger logger;

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
      CRITICAL_BAT_VOLTAGE,    // criticalBatVoltage
      CRITICAL_SLEEP_MINUTES,  // criticalSleepMinutes
      SLEEP_JITTER_MS,         // sleepJitterMs
      wakeRandom(),            // randomValue
  };

  // runNode() emits its own diagnostics (via logger) before it sleeps, since
  // clock.deepSleep() never returns on real hardware — anything logged here
  // after the call would never actually run.
  runNode(cfg, bootCount, txCount, versionReported, sensor, radio, display, power, clock, logger);
}

void loop() {
  // Never reached
}
