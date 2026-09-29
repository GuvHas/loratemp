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

// ---------- shouldForceDisplay ----------

void test_shouldForceDisplay_false_when_all_healthy() {
  TEST_ASSERT_FALSE(shouldForceDisplay(true, true, false));
}

void test_shouldForceDisplay_true_on_dht_failure() {
  TEST_ASSERT_TRUE(shouldForceDisplay(false, true, false));
}

void test_shouldForceDisplay_true_on_lora_failure() {
  TEST_ASSERT_TRUE(shouldForceDisplay(true, false, false));
}

void test_shouldForceDisplay_true_on_low_battery() {
  TEST_ASSERT_TRUE(shouldForceDisplay(true, true, true));
}

void test_shouldForceDisplay_true_when_everything_fails() {
  TEST_ASSERT_TRUE(shouldForceDisplay(false, false, true));
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

  RUN_TEST(test_shouldForceDisplay_false_when_all_healthy);
  RUN_TEST(test_shouldForceDisplay_true_on_dht_failure);
  RUN_TEST(test_shouldForceDisplay_true_on_lora_failure);
  RUN_TEST(test_shouldForceDisplay_true_on_low_battery);
  RUN_TEST(test_shouldForceDisplay_true_when_everything_fails);

  return UNITY_END();
}
