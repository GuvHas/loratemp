#include "hal_esp32.h"

#include <Arduino.h>
#include <LoRa.h>
#include <SPI.h>

// ---------- DhtSensor ----------

DhtSensor::DhtSensor(uint8_t pin, uint8_t type) : dht_(pin, type) {}

void DhtSensor::begin() { dht_.begin(); }

SensorReading DhtSensor::read() {
  float t = dht_.readTemperature();
  float h = dht_.readHumidity();
  bool ok = !isnan(t) && !isnan(h);
  return SensorReading{t, h, ok};
}

// ---------- LoRaRadioAdapter ----------

LoRaRadioAdapter::LoRaRadioAdapter(int sckPin, int misoPin, int mosiPin, int ssPin, int rstPin,
                                    int di0Pin, long band, int spreadingFactor, int txPowerDbm,
                                    int ledPin)
    : sckPin_(sckPin),
      misoPin_(misoPin),
      mosiPin_(mosiPin),
      ssPin_(ssPin),
      rstPin_(rstPin),
      di0Pin_(di0Pin),
      band_(band),
      spreadingFactor_(spreadingFactor),
      txPowerDbm_(txPowerDbm),
      ledPin_(ledPin) {}

bool LoRaRadioAdapter::begin() {
  SPI.begin(sckPin_, misoPin_, mosiPin_, ssPin_);
  LoRa.setPins(ssPin_, rstPin_, di0Pin_);
  if (!LoRa.begin(band_)) {
    return false;
  }
  LoRa.setSpreadingFactor(spreadingFactor_);
  LoRa.setTxPower(txPowerDbm_);
  LoRa.enableCrc();
  return true;
}

bool LoRaRadioAdapter::send(const char* msg) {
  digitalWrite(ledPin_, HIGH);
  LoRa.beginPacket();
  LoRa.print(msg);
  int result = LoRa.endPacket();
  digitalWrite(ledPin_, LOW);
  return result != 0;
}

void LoRaRadioAdapter::end() {
  LoRa.end();
  digitalWrite(ledPin_, LOW);
}

// ---------- Ssd1306Display ----------

Ssd1306Display::Ssd1306Display(uint8_t address, int sdaPin, int sclPin)
    : display_(address, sdaPin, sclPin) {}

void Ssd1306Display::init() {
  display_.init();
  display_.flipScreenVertically();
  display_.setFont(ArialMT_Plain_10);
  display_.drawString(0, 0, "Reading Sensor...");
  display_.display();
}

void Ssd1306Display::showReading(const SensorReading& reading, float voltage, bool lowBat,
                                  bool sent) {
  display_.clear();
  display_.drawString(0, 0, sent ? "Sent OK:" : "TX FAILED:");
  if (reading.valid) {
    display_.drawString(0, 15, "T: " + String(reading.temperatureC, 1) + " \xb0" + "C");
    display_.drawString(0, 30, "H: " + String(reading.humidityPct, 1) + " %");
  } else {
    display_.drawString(0, 15, "DHT: FAILED");
  }
  display_.drawString(0, 45, "Bat: " + String(voltage, 2) + "V" + (lowBat ? " LOW!" : ""));
  display_.display();
}

void Ssd1306Display::off() { display_.displayOff(); }

// ---------- AdcPower ----------

AdcPower::AdcPower(int batPin) : batPin_(batPin) {}

float AdcPower::readBatteryVoltage() {
  // Average multiple ADC samples to reduce noise
  const int samples = 10;
  long total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(batPin_);
    delay(2);
  }
  float reading = (float)total / samples;

  // 3.3V reference / 4095 steps * 2 (voltage divider ratio)
  return (reading / 4095.0f) * 3.3f * 2.0f;
}

// ---------- Esp32Clock ----------

void Esp32Clock::delayMs(uint32_t ms) { delay(ms); }

void Esp32Clock::deepSleep(uint64_t micros) {
  esp_sleep_enable_timer_wakeup(micros);
  esp_deep_sleep_start();
}
