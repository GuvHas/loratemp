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
// Returns the number of bytes written (excluding the null terminator).
int formatPayload(char* buf, size_t bufSize, const char* nodeId,
                   const SensorReading& reading, float batteryVoltage,
                   float lowBatteryThreshold, uint32_t bootCount,
                   uint32_t txCount);

// Deep-sleep duration in microseconds, for esp_sleep_enable_timer_wakeup.
uint64_t sleepMicros(int sleepMinutes);

// True on the regular display cadence: the very first boot, or every
// Nth boot thereafter.
bool isScheduledDisplayBoot(uint32_t bootCount, int displayEveryN);

// True if the display should be forced on outside its normal cadence
// because something needs the operator's attention.
bool shouldForceDisplay(bool dhtOk, bool loraOk, bool lowBattery);
