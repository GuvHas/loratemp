#include "orchestrator.h"

#include <cstdio>
#include <cstring>

namespace {

void goToSleep(const NodeConfig& cfg, IClock& clock, ILoRaRadio& radio, IDisplay& display,
               bool displayed, bool criticalBattery) {
  radio.end();
  if (displayed) {
    display.off();
  }
  int minutes = criticalBattery ? cfg.criticalSleepMinutes : cfg.sleepMinutes;
  clock.deepSleep(jitteredSleepMicros(minutes, cfg.sleepJitterMs, cfg.randomValue));
}

}  // namespace

NodeOutcome runNode(const NodeConfig& cfg, uint32_t bootCount, uint32_t& txCount,
                     bool& versionReported, ISensor& sensor, ILoRaRadio& radio,
                     IDisplay& display, IPower& power, IClock& clock, ILogger& logger) {
  NodeOutcome outcome{};
  outcome.payload[0] = '\0';

  bool showDisplay = isScheduledDisplayBoot(bootCount, cfg.displayEveryN);
  if (showDisplay) {
    display.init();
  }

  sensor.begin();
  clock.idleMs(cfg.dhtSettleDelayMs);  // let sensor and voltage rail stabilize

  SensorReading reading{0.0f, 0.0f, false};
  for (int attempt = 0; attempt < cfg.dhtMaxRetries; attempt++) {
    reading = sensor.read();
    if (reading.valid) {
      break;
    }
    if (attempt < cfg.dhtMaxRetries - 1) {
      clock.idleMs(cfg.dhtRetryDelayMs);
    }
  }
  outcome.dhtOk = reading.valid;
  if (!outcome.dhtOk) {
    logger.log("DHT Read Failed after retries!");
  }

  float voltage = power.readBatteryVoltage();
  bool lowBat = voltage < cfg.lowBatVoltage;
  bool criticalBat = voltage < cfg.criticalBatVoltage;
  if (criticalBat) {
    logger.log("Battery critical, sleeping longer");
  }

  if (!radio.begin()) {
    logger.log("LoRa Init Failed!");
    outcome.displayed = showDisplay;
    goToSleep(cfg, clock, radio, display, showDisplay, criticalBat);
    return outcome;
  }

  txCount++;
  // Only reported while the gateway hasn't actually confirmed receipt yet
  // (see versionReported's doc comment on runNode()); decided once, before
  // the send attempts below, so a mid-cycle flip doesn't change what this
  // specific payload claims partway through its own retries.
  bool reportVersion = shouldReportVersion(versionReported, txCount, cfg.sleepMinutes);

  uint8_t packet[sizeof(outcome.payload)];
  size_t packetLen = 0;
  int frameLen = -1;
  if (cfg.binaryPayload) {
    frameLen = encodeFrame(packet, sizeof(packet), cfg.nodeId, reading, voltage,
                           cfg.lowBatVoltage, bootCount, txCount,
                           reportVersion ? nodeFirmwareVersion() : nullptr);
  }
  if (frameLen > 0) {
    packetLen = static_cast<size_t>(frameLen);
    // Log text: "frame " + hex, truncated to fit if the frame is very long.
    static const char* kHex = "0123456789ABCDEF";
    size_t pos = static_cast<size_t>(snprintf(outcome.payload, sizeof(outcome.payload), "frame "));
    for (size_t i = 0; i < packetLen && pos + 2 < sizeof(outcome.payload); i++) {
      outcome.payload[pos++] = kHex[packet[i] >> 4];
      outcome.payload[pos++] = kHex[packet[i] & 0x0F];
    }
    outcome.payload[pos] = '\0';
  } else {
    // JSON, also the fallback if a binary frame cannot be built (an id
    // longer than 255 bytes): formatPayload() always produces something.
    int len = formatPayload(outcome.payload, sizeof(outcome.payload), cfg.nodeId, reading,
                            voltage, cfg.lowBatVoltage, bootCount, txCount, reportVersion);
    packetLen = len > 0 ? static_cast<size_t>(len) : 0;
    memcpy(packet, outcome.payload, packetLen);
  }

  char sendMsg[sizeof(outcome.payload) + 16];
  snprintf(sendMsg, sizeof(sendMsg), "Sending: %s", outcome.payload);
  logger.log(sendMsg);

  bool sent = false;
  for (int attempt = 0; attempt < cfg.loraMaxRetries; attempt++) {
    sent = radio.send(packet, packetLen);
    if (sent) {
      break;
    }
    if (attempt < cfg.loraMaxRetries - 1) {
      clock.delayMs(cfg.loraRetryDelayMs);
    }
  }
  outcome.loraOk = sent;
  if (!outcome.loraOk) {
    logger.log("LoRa TX failed after retries!");
  } else if (reportVersion) {
    // The payload that just went out over the air actually carried "sw";
    // only now is it safe to stop repeating it. Left false on any failure
    // above (or if radio.begin() failed before this point at all) so the
    // next wake retries with "sw" included again.
    versionReported = true;
  }

  if (!showDisplay && shouldForceDisplay(outcome.dhtOk, outcome.loraOk)) {
    showDisplay = true;
    display.init();
  }

  outcome.displayed = showDisplay;
  if (showDisplay) {
    display.showReading(reading, voltage, lowBat, outcome.loraOk);
    clock.idleMs(cfg.displaySeconds * 1000);
  }

  goToSleep(cfg, clock, radio, display, showDisplay, criticalBat);
  return outcome;
}
