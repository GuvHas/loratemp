#pragma once

// Thin ESP32/Arduino adapters implementing the hal.h interfaces. No retry,
// scheduling, or formatting logic lives here — that's all in orchestrator.cpp.
// Not built for the native test environment (see platformio.ini).

#include <DHT.h>
#include <SSD1306.h>

#include "hal.h"

class DhtSensor : public ISensor {
 public:
  DhtSensor(uint8_t pin, uint8_t type);
  void begin() override;
  SensorReading read() override;

 private:
  DHT dht_;
};

class LoRaRadioAdapter : public ILoRaRadio {
 public:
  LoRaRadioAdapter(int sckPin, int misoPin, int mosiPin, int ssPin, int rstPin, int di0Pin,
                    long band, int spreadingFactor, int txPowerDbm, int ledPin);
  bool begin() override;
  bool send(const char* msg) override;
  void end() override;

 private:
  int sckPin_, misoPin_, mosiPin_, ssPin_, rstPin_, di0Pin_;
  long band_;
  int spreadingFactor_;
  int txPowerDbm_;
  int ledPin_;
};

class Ssd1306Display : public IDisplay {
 public:
  Ssd1306Display(uint8_t address, int sdaPin, int sclPin);
  void init() override;
  void showReading(const SensorReading& reading, float voltage, bool lowBat, bool sent) override;
  void off() override;

 private:
  SSD1306 display_;
};

class AdcPower : public IPower {
 public:
  explicit AdcPower(int batPin);
  float readBatteryVoltage() override;

 private:
  int batPin_;
};

class Esp32Clock : public IClock {
 public:
  void delayMs(uint32_t ms) override;
  void deepSleep(uint64_t micros) override;
};
