#pragma once

#include <cstddef>
#include <cstdint>

// Pure, hardware-independent logic extracted from main.cpp so it can be
// exercised with native unit tests (no ESP32/Arduino toolchain required).

struct SensorReading {
  float temperatureC;
  float humidityPct;
  bool valid;  // false if the DHT22 read failed after all retries
};

// Builds the JSON payload sent over LoRa. Always emits every field (using
// JSON null for t/h when the reading is invalid) so downstream MQTT
// subscribers/HA templates never see a missing key. Falls back to a
// minimal error payload if the full message doesn't fit in buf, and to a
// fixed literal if even that doesn't fit.
//
// Appends "sw": NODE_FW_VERSION when includeSwVersion is true, and omits it
// entirely otherwise. The caller (runNode(), via its versionReported
// parameter) decides this based on whether the gateway has actually
// confirmed receipt of this node's version yet -- not simply "is this boot
// number 1" -- so a cold boot whose LoRa radio fails to init, or whose send
// never gets through after all retries, keeps trying on every subsequent
// wake instead of permanently missing its one window to report (a node has
// no OTA path, so losing that window meant not until the next physical
// flash/battery-swap). Every packet after a *successful* report omits "sw"
// entirely: the gateway remembers whatever version it was last told (see
// loragateway's GatewayOrchestrator::swVersionByNode_), so repeating a
// value that can't have changed since the last packet would waste airtime
// and battery for nothing.
//
// Returns the number of bytes written (excluding the null terminator).
int formatPayload(char* buf, size_t bufSize, const char* nodeId,
                   const SensorReading& reading, float batteryVoltage,
                   float lowBatteryThreshold, uint32_t bootCount,
                   uint32_t txCount, bool includeSwVersion);

// Deep-sleep duration in microseconds, for esp_sleep_enable_timer_wakeup.
uint64_t sleepMicros(int sleepMinutes);

// True on the regular display cadence: the very first boot, or every
// Nth boot thereafter.
bool isScheduledDisplayBoot(uint32_t bootCount, int displayEveryN);

// True if the display should be forced on outside its normal cadence
// because something needs the operator's attention.
bool shouldForceDisplay(bool dhtOk, bool loraOk, bool lowBattery);
