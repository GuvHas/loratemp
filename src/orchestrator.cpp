#include "orchestrator.h"

#include <cstdio>

namespace {

void goToSleep(IClock& clock, ILoRaRadio& radio, IDisplay& display, bool displayed,
               int sleepMinutes) {
  radio.end();
  if (displayed) {
    display.off();
  }
  clock.deepSleep(sleepMicros(sleepMinutes));
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
  clock.delayMs(cfg.dhtSettleDelayMs);  // let sensor and voltage rail stabilize

  SensorReading reading{0.0f, 0.0f, false};
  for (int attempt = 0; attempt < cfg.dhtMaxRetries; attempt++) {
    reading = sensor.read();
    if (reading.valid) {
      break;
    }
    if (attempt < cfg.dhtMaxRetries - 1) {
      clock.delayMs(cfg.dhtRetryDelayMs);
    }
  }
  outcome.dhtOk = reading.valid;
  if (!outcome.dhtOk) {
    logger.log("DHT Read Failed after retries!");
  }

  float voltage = power.readBatteryVoltage();
  bool lowBat = voltage < cfg.lowBatVoltage;

  if (!radio.begin()) {
    logger.log("LoRa Init Failed!");
    outcome.displayed = showDisplay;
    goToSleep(clock, radio, display, showDisplay, cfg.sleepMinutes);
    return outcome;
  }

  txCount++;
  // Only reported while the gateway hasn't actually confirmed receipt yet
  // (see versionReported's doc comment on runNode()); decided once, before
  // the send attempts below, so a mid-cycle flip doesn't change what this
  // specific payload claims partway through its own retries.
  bool reportVersion = !versionReported;
  formatPayload(outcome.payload, sizeof(outcome.payload), cfg.nodeId, reading, voltage,
                cfg.lowBatVoltage, bootCount, txCount, reportVersion);

  char sendMsg[sizeof(outcome.payload) + 16];
  snprintf(sendMsg, sizeof(sendMsg), "Sending: %s", outcome.payload);
  logger.log(sendMsg);

  bool sent = false;
  for (int attempt = 0; attempt < cfg.loraMaxRetries; attempt++) {
    sent = radio.send(outcome.payload);
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

  if (!showDisplay && shouldForceDisplay(outcome.dhtOk, outcome.loraOk, lowBat)) {
    showDisplay = true;
    display.init();
  }

  outcome.displayed = showDisplay;
  if (showDisplay) {
    display.showReading(reading, voltage, lowBat, outcome.loraOk);
    clock.delayMs(cfg.displaySeconds * 1000);
  }

  goToSleep(clock, radio, display, showDisplay, cfg.sleepMinutes);
  return outcome;
}
