#include "orchestrator.h"

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
                     ISensor& sensor, ILoRaRadio& radio, IDisplay& display,
                     IPower& power, IClock& clock) {
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

  float voltage = power.readBatteryVoltage();
  bool lowBat = voltage < cfg.lowBatVoltage;

  if (!radio.begin()) {
    goToSleep(clock, radio, display, showDisplay, cfg.sleepMinutes);
    return outcome;
  }

  txCount++;
  formatPayload(outcome.payload, sizeof(outcome.payload), cfg.nodeId, reading, voltage,
                cfg.lowBatVoltage, bootCount, txCount);

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
