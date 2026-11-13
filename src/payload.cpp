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

uint64_t sleepMicros(int sleepMinutes) {
  return static_cast<uint64_t>(sleepMinutes) * 60ULL * 1000000ULL;
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
