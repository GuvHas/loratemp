#pragma once

#include <cstddef>
#include <cstdint>

#include "payload.h"

// Hardware abstraction interfaces. These have zero dependency on
// Arduino/ESP-IDF so orchestrator.cpp can be linked and unit-tested
// natively against fakes. Concrete ESP32 adapters live in hal_esp32.h/.cpp.

class ISensor {
 public:
  virtual ~ISensor() = default;
  virtual void begin() = 0;
  virtual SensorReading read() = 0;  // one attempt; caller handles retries
};

class ILoRaRadio {
 public:
  virtual ~ILoRaRadio() = default;
  virtual bool begin() = 0;
  // One attempt; caller handles retries. Takes bytes and a length, since a
  // binary frame contains NULs.
  virtual bool send(const uint8_t* data, size_t len) = 0;
  virtual void end() = 0;
};

class IDisplay {
 public:
  virtual ~IDisplay() = default;
  virtual void init() = 0;
  virtual void showReading(const SensorReading& reading, float voltage, bool lowBat, bool sent) = 0;
  virtual void off() = 0;
};

class IPower {
 public:
  virtual ~IPower() = default;
  virtual float readBatteryVoltage() = 0;
};

class IClock {
 public:
  virtual ~IClock() = default;
  // Busy wait: the CPU stays awake. Only for short waits around the radio.
  virtual void delayMs(uint32_t ms) = 0;
  // Low-power wait for something that needs time but not the CPU (sensor
  // settling, holding the OLED). Light sleep on hardware: GPIO levels, RAM,
  // the radio's registers and the OLED's framebuffer are all kept.
  virtual void idleMs(uint32_t ms) = 0;
  virtual void deepSleep(uint64_t micros) = 0;  // never returns on real hardware
};

// Diagnostic logging. A separate interface (rather than folding into
// IClock) because runNode() must emit each message at the moment it's
// known — before the final deepSleep() call, which never returns on real
// hardware, so any logging attempted after runNode() returns is dead code.
class ILogger {
 public:
  virtual ~ILogger() = default;
  virtual void log(const char* msg) = 0;
};
