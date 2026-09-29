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
// versionReported is the caller's RTC-persisted "has the gateway actually
// received this node's firmware version yet" flag (false only right after a
// real power-on, since RTC memory resets then): while false, the outgoing
// payload includes "sw" (see formatPayload()), and this only flips it to
// true once a send carrying that field actually succeeds -- so a cold boot
// whose radio fails to init, or whose every send attempt fails, leaves it
// false and keeps retrying on the next wake, rather than permanently
// missing its one window to report (this node has no OTA path, so losing
// that window meant not reporting again until a physical flash/battery
// swap; Codex review on loratemp PR #12).
// Always ends by calling clock.deepSleep(), which never returns on real
// hardware, so every code path — success, DHT failure, LoRa failure, or
// LoRa init failure — is guaranteed to put the node back to sleep.
NodeOutcome runNode(const NodeConfig& cfg, uint32_t bootCount, uint32_t& txCount,
                     bool& versionReported, ISensor& sensor, ILoRaRadio& radio,
                     IDisplay& display, IPower& power, IClock& clock, ILogger& logger);
