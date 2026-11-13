#include <unity.h>

#include <cstring>

#include "payload.h"

void setUp() {}
void tearDown() {}

// ---------- formatPayload ----------

void test_formatPayload_valid_reading() {
  char buf[192];
  SensorReading reading{21.5f, 55.2f, true};
  int len = formatPayload(buf, sizeof(buf), "GarageTemp", reading, 3.9f, 3.3f, 7, 4,
                           /*includeSwVersion=*/false);

  TEST_ASSERT_GREATER_THAN(0, len);
  TEST_ASSERT_EQUAL_STRING(
      "{\"id\":\"GarageTemp\",\"t\":21.5,\"h\":55.2,\"v\":3.90,\"boot\":7,\"seq\":4,\"lb\":0,\"err\":\"none\"}",
      buf);
}

void test_formatPayload_dht_failure_emits_null_fields() {
  char buf[192];
  SensorReading reading{0.0f, 0.0f, false};
  formatPayload(buf, sizeof(buf), "GarageTemp", reading, 3.9f, 3.3f, 1, 1, /*includeSwVersion=*/false);

  TEST_ASSERT_EQUAL_STRING(
      "{\"id\":\"GarageTemp\",\"t\":null,\"h\":null,\"v\":3.90,\"boot\":1,\"seq\":1,\"lb\":0,\"err\":\"dht\"}",
      buf);
}

void test_formatPayload_low_battery_flag_set() {
  char buf[192];
  SensorReading reading{20.0f, 50.0f, true};
  formatPayload(buf, sizeof(buf), "GarageTemp", reading, 3.1f, 3.3f, 1, 1, /*includeSwVersion=*/false);

  TEST_ASSERT_NOT_NULL(strstr(buf, "\"lb\":1"));
}

void test_formatPayload_low_battery_flag_clear_at_threshold() {
  char buf[192];
  SensorReading reading{20.0f, 50.0f, true};
  // Voltage exactly at threshold is not "low" (strict less-than).
  formatPayload(buf, sizeof(buf), "GarageTemp", reading, 3.3f, 3.3f, 1, 1, /*includeSwVersion=*/false);

  TEST_ASSERT_NOT_NULL(strstr(buf, "\"lb\":0"));
}

// ---------- "sw" field ----------
// Whether "sw" is included is entirely the caller's decision (see
// runNode()'s versionReported tracking in orchestrator.cpp) -- formatPayload
// itself has no opinion on bootCount for this.

void test_formatPayload_includes_sw_field_when_requested() {
  char buf[192];
  SensorReading reading{21.5f, 55.2f, true};
  formatPayload(buf, sizeof(buf), "GarageTemp", reading, 3.9f, 3.3f, 1, 1, /*includeSwVersion=*/true);

  TEST_ASSERT_EQUAL_STRING(
      "{\"id\":\"GarageTemp\",\"t\":21.5,\"h\":55.2,\"v\":3.90,\"boot\":1,\"seq\":1,\"lb\":0,\"err\":\"none\""
      ",\"sw\":\"dev\"}",
      buf);
}

void test_formatPayload_omits_sw_field_when_not_requested() {
  char buf[192];
  SensorReading reading{21.5f, 55.2f, true};
  // Must not carry "sw" at all (not even null) when not requested, to avoid
  // spending airtime/battery on a value that can't have changed since the
  // last transmission the gateway actually received.
  formatPayload(buf, sizeof(buf), "GarageTemp", reading, 3.9f, 3.3f, 2, 1, /*includeSwVersion=*/false);

  TEST_ASSERT_NULL(strstr(buf, "\"sw\""));
}

void test_formatPayload_falls_back_when_full_message_does_not_fit() {
  // Full payload needs 84 bytes; the "fmt" fallback needs 49. This buffer
  // fits the fallback but not the full message.
  char buf[60];
  SensorReading reading{21.5f, 55.2f, true};
  int len = formatPayload(buf, sizeof(buf), "GarageTemp", reading, 3.9f, 3.3f, 7, 4,
                           /*includeSwVersion=*/false);

  TEST_ASSERT_EQUAL_STRING("{\"id\":\"GarageTemp\",\"boot\":7,\"seq\":4,\"err\":\"fmt\"}", buf);
  TEST_ASSERT_EQUAL(static_cast<int>(strlen(buf)), len);
}

void test_formatPayload_falls_back_to_fixed_literal_when_nothing_fits() {
  char buf[8];  // too small even for the "fmt" fallback message (needs 49)
  SensorReading reading{21.5f, 55.2f, true};
  formatPayload(buf, sizeof(buf), "GarageTemp", reading, 3.9f, 3.3f, 7, 4, /*includeSwVersion=*/false);

  // Truncated literal, always null-terminated within bufSize.
  TEST_ASSERT_EQUAL_STRING("{\"id\":\"", buf);
  TEST_ASSERT_EQUAL('\0', buf[sizeof(buf) - 1]);
}

// ---------- sleepMicros ----------

void test_sleepMicros_converts_minutes_to_micros() {
  TEST_ASSERT_EQUAL_UINT64(5ULL * 60ULL * 1000000ULL, sleepMicros(5));
  TEST_ASSERT_EQUAL_UINT64(0ULL, sleepMicros(0));
  TEST_ASSERT_EQUAL_UINT64(60ULL * 60ULL * 1000000ULL, sleepMicros(60));
}

// ---------- isScheduledDisplayBoot ----------

void test_isScheduledDisplayBoot_true_on_first_boot() {
  TEST_ASSERT_TRUE(isScheduledDisplayBoot(1, 12));
}

void test_isScheduledDisplayBoot_true_on_every_nth_boot() {
  TEST_ASSERT_TRUE(isScheduledDisplayBoot(12, 12));
  TEST_ASSERT_TRUE(isScheduledDisplayBoot(24, 12));
}

void test_isScheduledDisplayBoot_false_between_scheduled_boots() {
  TEST_ASSERT_FALSE(isScheduledDisplayBoot(2, 12));
  TEST_ASSERT_FALSE(isScheduledDisplayBoot(13, 12));
}

void test_isScheduledDisplayBoot_always_true_when_every_boot_configured() {
  TEST_ASSERT_TRUE(isScheduledDisplayBoot(1, 1));
  TEST_ASSERT_TRUE(isScheduledDisplayBoot(2, 1));
  TEST_ASSERT_TRUE(isScheduledDisplayBoot(99, 1));
}

// ---------- LoRa airtime and TX timeout ----------
// Reference values from the Semtech SX127x airtime formula for this
// project's settings: BW 125 kHz, CR 4/5, 8-symbol preamble, explicit
// header, CRC on.

void test_loraAirtimeMs_typical_json_payload_at_sf9() {
  TEST_ASSERT_EQUAL(472, (int)loraAirtimeMs(83, 9, 125000));
}

void test_loraAirtimeMs_short_payload_at_sf9() {
  TEST_ASSERT_EQUAL(206, (int)loraAirtimeMs(24, 9, 125000));
}

void test_loraAirtimeMs_at_sf7() {
  TEST_ASSERT_EQUAL(169, (int)loraAirtimeMs(98, 7, 125000));
}

void test_loraAirtimeMs_uses_low_data_rate_optimize_at_sf12() {
  // Symbol time 32.768 ms > 16 ms, so the formula's DE term is 1.
  TEST_ASSERT_EQUAL(3449, (int)loraAirtimeMs(83, 12, 125000));
}

void test_loraTxTimeoutMs_is_twice_airtime_plus_margin() {
  TEST_ASSERT_EQUAL(2 * 472 + 100, (int)loraTxTimeoutMs(83, 9, 125000));
}

void test_loraTxTimeoutMs_covers_largest_packet() {
  // 255 bytes is the most a LoRa packet can hold; the timeout must still
  // exceed its airtime, or every full-size send would be reported failed.
  TEST_ASSERT_GREATER_THAN(loraAirtimeMs(255, 9, 125000), loraTxTimeoutMs(255, 9, 125000));
}

// ---------- shouldForceDisplay ----------

void test_shouldForceDisplay_false_when_all_healthy() {
  TEST_ASSERT_FALSE(shouldForceDisplay(true, true));
}

void test_shouldForceDisplay_true_on_dht_failure() {
  TEST_ASSERT_TRUE(shouldForceDisplay(false, true));
}

void test_shouldForceDisplay_true_on_lora_failure() {
  TEST_ASSERT_TRUE(shouldForceDisplay(true, false));
}

void test_shouldForceDisplay_true_when_everything_fails() {
  TEST_ASSERT_TRUE(shouldForceDisplay(false, false));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_formatPayload_valid_reading);
  RUN_TEST(test_formatPayload_dht_failure_emits_null_fields);
  RUN_TEST(test_formatPayload_low_battery_flag_set);
  RUN_TEST(test_formatPayload_low_battery_flag_clear_at_threshold);
  RUN_TEST(test_formatPayload_includes_sw_field_when_requested);
  RUN_TEST(test_formatPayload_omits_sw_field_when_not_requested);
  RUN_TEST(test_formatPayload_falls_back_when_full_message_does_not_fit);
  RUN_TEST(test_formatPayload_falls_back_to_fixed_literal_when_nothing_fits);

  RUN_TEST(test_sleepMicros_converts_minutes_to_micros);

  RUN_TEST(test_isScheduledDisplayBoot_true_on_first_boot);
  RUN_TEST(test_isScheduledDisplayBoot_true_on_every_nth_boot);
  RUN_TEST(test_isScheduledDisplayBoot_false_between_scheduled_boots);
  RUN_TEST(test_isScheduledDisplayBoot_always_true_when_every_boot_configured);

  RUN_TEST(test_loraAirtimeMs_typical_json_payload_at_sf9);
  RUN_TEST(test_loraAirtimeMs_short_payload_at_sf9);
  RUN_TEST(test_loraAirtimeMs_at_sf7);
  RUN_TEST(test_loraAirtimeMs_uses_low_data_rate_optimize_at_sf12);
  RUN_TEST(test_loraTxTimeoutMs_is_twice_airtime_plus_margin);
  RUN_TEST(test_loraTxTimeoutMs_covers_largest_packet);

  RUN_TEST(test_shouldForceDisplay_false_when_all_healthy);
  RUN_TEST(test_shouldForceDisplay_true_on_dht_failure);
  RUN_TEST(test_shouldForceDisplay_true_on_lora_failure);
  RUN_TEST(test_shouldForceDisplay_true_when_everything_fails);

  return UNITY_END();
}
