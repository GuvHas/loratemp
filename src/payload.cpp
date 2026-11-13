#include "payload.h"

#include <cmath>
#include <cstdio>
#include <cstring>

// Injected at build time by scripts/inject_git_version.py (see
// platformio.ini) -- the current git short hash, mirroring loragateway's own
// GATEWAY_FW_VERSION. Falls back to "dev" only if that script somehow didn't
// run (e.g. building outside PlatformIO, or the native test env, which
// deliberately skips it).
#ifndef NODE_FW_VERSION
#define NODE_FW_VERSION "dev"
#endif

int formatPayload(char* buf, size_t bufSize, const char* nodeId,
                   const SensorReading& reading, float batteryVoltage,
                   float lowBatteryThreshold, uint32_t bootCount,
                   uint32_t txCount, bool includeSwVersion) {
  bool lowBat = batteryVoltage < lowBatteryThreshold;

  char t_str[8];
  char h_str[8];
  if (reading.valid) {
    snprintf(t_str, sizeof(t_str), "%.1f", reading.temperatureC);
    snprintf(h_str, sizeof(h_str), "%.1f", reading.humidityPct);
  } else {
    strcpy(t_str, "null");
    strcpy(h_str, "null");
  }

  // See payload.h's comment on why this is gated on the caller's
  // includeSwVersion decision, not directly on bootCount.
  int len;
  if (includeSwVersion) {
    len = snprintf(buf, bufSize,
                    "{\"id\":\"%s\",\"t\":%s,\"h\":%s,\"v\":%.2f"
                    ",\"boot\":%lu,\"seq\":%lu,\"lb\":%d,\"err\":\"%s\""
                    ",\"sw\":\"%s\"}",
                    nodeId, t_str, h_str, batteryVoltage,
                    static_cast<unsigned long>(bootCount),
                    static_cast<unsigned long>(txCount), lowBat ? 1 : 0,
                    reading.valid ? "none" : "dht", NODE_FW_VERSION);
  } else {
    len = snprintf(buf, bufSize,
                    "{\"id\":\"%s\",\"t\":%s,\"h\":%s,\"v\":%.2f"
                    ",\"boot\":%lu,\"seq\":%lu,\"lb\":%d,\"err\":\"%s\"}",
                    nodeId, t_str, h_str, batteryVoltage,
                    static_cast<unsigned long>(bootCount),
                    static_cast<unsigned long>(txCount), lowBat ? 1 : 0,
                    reading.valid ? "none" : "dht");
  }

  bool ok = len > 0 && static_cast<size_t>(len) < bufSize;
  if (!ok) {
    len = snprintf(buf, bufSize, "{\"id\":\"%s\",\"boot\":%lu,\"seq\":%lu,\"err\":\"fmt\"}",
                    nodeId, static_cast<unsigned long>(bootCount),
                    static_cast<unsigned long>(txCount));
    ok = len > 0 && static_cast<size_t>(len) < bufSize;
    if (!ok) {
      static const char* fallback = "{\"id\":\"unknown\",\"err\":\"fmt\"}";
      strncpy(buf, fallback, bufSize);
      if (bufSize > 0) {
        buf[bufSize - 1] = '\0';
      }
      len = static_cast<int>(strlen(buf));
    }
  }

  return len;
}

const char* nodeFirmwareVersion() { return NODE_FW_VERSION; }

namespace {

void putU24(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
}

void putU16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

long clampLong(long v, long lo, long hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

int encodeFrame(uint8_t* buf, size_t bufSize, const char* nodeId, const SensorReading& reading,
                float batteryVoltage, float lowBatteryThreshold, uint32_t bootCount,
                uint32_t txCount, const char* swVersion) {
  const uint8_t kMagic = 0xC1;
  const uint8_t kFlagLowBattery = 0x01;
  const uint8_t kFlagSensorError = 0x02;
  const uint8_t kFlagSwPresent = 0x04;

  size_t idLen = nodeId ? strlen(nodeId) : 0;
  if (idLen == 0 || idLen > 255) {
    return -1;
  }
  size_t swLen = swVersion ? strlen(swVersion) : 0;
  if (swLen > 255) {
    swLen = 255;
  }
  size_t total = 3 + idLen + 11 + (swLen > 0 ? 1 + swLen : 0);
  if (buf == nullptr || total > bufSize) {
    return -1;
  }

  uint8_t flags = 0;
  if (batteryVoltage < lowBatteryThreshold) flags |= kFlagLowBattery;
  if (!reading.valid) flags |= kFlagSensorError;
  if (swLen > 0) flags |= kFlagSwPresent;

  buf[0] = kMagic;
  buf[1] = flags;
  buf[2] = static_cast<uint8_t>(idLen);
  memcpy(buf + 3, nodeId, idLen);
  uint8_t* p = buf + 3 + idLen;
  putU24(p, bootCount & 0xFFFFFFu);
  putU24(p + 3, txCount & 0xFFFFFFu);

  // 0x8000 and 0xFFFF are the null markers, so real values stop one short.
  uint16_t t = 0x8000;
  uint16_t h = 0xFFFF;
  if (reading.valid) {
    t = static_cast<uint16_t>(static_cast<int16_t>(
        clampLong(std::lround(reading.temperatureC * 10.0f), -32767, 32767)));
    h = static_cast<uint16_t>(clampLong(std::lround(reading.humidityPct * 10.0f), 0, 0xFFFE));
  }
  putU16(p + 6, t);
  putU16(p + 8, h);
  p[10] = static_cast<uint8_t>(clampLong(std::lround(batteryVoltage * 100.0f) - 200, 0, 255));

  if (swLen > 0) {
    p[11] = static_cast<uint8_t>(swLen);
    memcpy(p + 12, swVersion, swLen);
  }
  return static_cast<int>(total);
}

bool shouldReportVersion(bool versionReported, uint32_t txCount, int sleepMinutes) {
  if (!versionReported) return true;
  if (txCount >= 1 && txCount <= 3) return true;
  if (sleepMinutes <= 0) return false;
  uint32_t sendsPerDay = static_cast<uint32_t>(24 * 60 / sleepMinutes);
  return sendsPerDay > 0 && txCount % sendsPerDay == 0;
}

uint64_t sleepMicros(int sleepMinutes) {
  return static_cast<uint64_t>(sleepMinutes) * 60ULL * 1000000ULL;
}

uint64_t jitteredSleepMicros(int sleepMinutes, int jitterMaxMs, uint32_t randomValue) {
  int64_t baseMs = static_cast<int64_t>(sleepMicros(sleepMinutes) / 1000ULL);
  int64_t offsetMs = 0;
  if (jitterMaxMs > 0) {
    uint32_t span = 2u * static_cast<uint32_t>(jitterMaxMs) + 1u;
    offsetMs = static_cast<int64_t>(randomValue % span) - jitterMaxMs;
  }
  int64_t ms = baseMs + offsetMs;
  if (ms < 1000) {
    ms = 1000;
  }
  return static_cast<uint64_t>(ms) * 1000ULL;
}

uint32_t loraAirtimeMs(size_t payloadLen, int spreadingFactor, long bandwidthHz) {
  const double preambleSymbols = 8.0;
  const int codingRate = 1;  // 4/5
  const int crc = 1;
  const int implicitHeader = 0;
  double symbolMs = std::ldexp(1.0, spreadingFactor) * 1000.0 / static_cast<double>(bandwidthHz);
  int lowDataRate = symbolMs > 16.0 ? 1 : 0;
  double numerator = 8.0 * static_cast<double>(payloadLen) - 4.0 * spreadingFactor + 28.0 +
                     16.0 * crc - 20.0 * implicitHeader;
  double denominator = 4.0 * (spreadingFactor - 2 * lowDataRate);
  double extra = std::ceil(numerator / denominator) * (codingRate + 4);
  double payloadSymbols = 8.0 + (extra > 0.0 ? extra : 0.0);
  double totalMs = (preambleSymbols + 4.25 + payloadSymbols) * symbolMs;
  return static_cast<uint32_t>(std::lround(totalMs));
}

uint32_t loraTxTimeoutMs(size_t payloadLen, int spreadingFactor, long bandwidthHz) {
  return 2 * loraAirtimeMs(payloadLen, spreadingFactor, bandwidthHz) + 100;
}

bool isScheduledDisplayBoot(uint32_t bootCount, int displayEveryN) {
  return bootCount == 1 || (displayEveryN > 0 && bootCount % displayEveryN == 0);
}

bool shouldForceDisplay(bool dhtOk, bool loraOk) { return !dhtOk || !loraOk; }
