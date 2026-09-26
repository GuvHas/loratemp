#pragma once

#include <string>
#include <vector>

#include "hal.h"
#include "orchestrator.h"

// Test doubles for hal.h, used to drive the orchestrator through
// hardware edge cases (timeouts, TX failures) without any real hardware.

class FakeSensor : public ISensor {
 public:
  std::vector<SensorReading> queuedReads;  // one entry consumed per read() call
  int beginCalls = 0;
  int readCalls = 0;

  void begin() override { beginCalls++; }

  SensorReading read() override {
    SensorReading result = readCalls < static_cast<int>(queuedReads.size())
                                ? queuedReads[readCalls]
                                : SensorReading{0.0f, 0.0f, false};
    readCalls++;
    return result;
  }
};

class FakeRadio : public ILoRaRadio {
 public:
  bool beginResult = true;
  std::vector<bool> queuedSendResults;  // one entry consumed per send() call
  std::vector<std::string> sentPayloads;
  int beginCalls = 0;
  int sendCalls = 0;
  int endCalls = 0;

  bool begin() override {
    beginCalls++;
    return beginResult;
  }

  bool send(const char* msg) override {
    sentPayloads.push_back(msg);
    bool result = sendCalls < static_cast<int>(queuedSendResults.size())
                      ? queuedSendResults[sendCalls]
                      : false;
    sendCalls++;
    return result;
  }

  void end() override { endCalls++; }
};

class FakeDisplay : public IDisplay {
 public:
  int initCalls = 0;
  int showReadingCalls = 0;
  int offCalls = 0;
  bool lastSent = false;
  bool lastLowBat = false;
  SensorReading lastReading{0.0f, 0.0f, false};

  void init() override { initCalls++; }

  void showReading(const SensorReading& reading, float voltage, bool lowBat, bool sent) override {
    (void)voltage;
    showReadingCalls++;
    lastReading = reading;
    lastLowBat = lowBat;
    lastSent = sent;
  }

  void off() override { offCalls++; }
};

class FakePower : public IPower {
 public:
  float voltage = 3.9f;

  float readBatteryVoltage() override { return voltage; }
};

class FakeClock : public IClock {
 public:
  std::vector<uint32_t> delaysMs;
  uint64_t sleptMicros = 0;
  int sleepCalls = 0;

  void delayMs(uint32_t ms) override { delaysMs.push_back(ms); }

  void deepSleep(uint64_t micros) override {
    sleptMicros = micros;
    sleepCalls++;
  }
};

inline NodeConfig defaultTestConfig() {
  return NodeConfig{
      "TestNode",  // nodeId
      3,           // dhtMaxRetries
      2000,        // dhtRetryDelayMs
      2000,        // dhtSettleDelayMs
      2,           // loraMaxRetries
      100,         // loraRetryDelayMs
      3.3f,        // lowBatVoltage
      5,           // sleepMinutes
      12,          // displayEveryN
      2,           // displaySeconds
  };
}
