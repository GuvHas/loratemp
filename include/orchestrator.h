#pragma once

#include <cstdint>

#include "hal.h"
#include "payload.h"

struct NodeConfig {
  const char* nodeId;
  int dhtMaxRetries;
  int dhtRetryDelayMs;
  int dhtSettleDelayMs;
  int loraMaxRetries;
  int loraRetryDelayMs;
  float lowBatVoltage;
  int sleepMinutes;
  int displayEveryN;
  int displaySeconds;
};

struct NodeOutcome {
  bool dhtOk;
  bool loraOk;
  bool displayed;
  char payload[192];
};

// Runs one full sensor-node cycle: read (with retry), transmit (with
// retry), decide whether to light the display, and sleep. bootCount is
// the caller's already-incremented per-boot counter; txCount is the
// caller's RTC-persisted send counter, incremented here only when a send
// is actually attempted (matching the original firmware's behavior).
// Always ends by calling clock.deepSleep(), which never returns on real
// hardware, so every code path — success, DHT failure, LoRa failure, or
// LoRa init failure — is guaranteed to put the node back to sleep.
NodeOutcome runNode(const NodeConfig& cfg, uint32_t bootCount, uint32_t& txCount,
                     ISensor& sensor, ILoRaRadio& radio, IDisplay& display,
                     IPower& power, IClock& clock, ILogger& logger);
