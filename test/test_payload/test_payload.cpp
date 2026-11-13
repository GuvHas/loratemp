#include <unity.h>

#include <cstring>
#include <string>

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

// ---------- jitteredSleepMicros ----------
// Every node sleeping exactly 5 minutes keeps two nodes that were powered up
// together transmitting at the same moment, colliding on every wake. A
// random offset of up to +/- jitterMaxMs spreads them apart.

void test_jitteredSleepMicros_without_jitter_is_exact() {
  TEST_ASSERT_EQUAL_UINT64(sleepMicros(5), jitteredSleepMicros(5, 0, 12345));
}

void test_jitteredSleepMicros_lowest_random_value_gives_minus_jitter() {
  TEST_ASSERT_EQUAL_UINT64(sleepMicros(5) - 5000ULL * 1000ULL, jitteredSleepMicros(5, 5000, 0));
}

void test_jitteredSleepMicros_spans_plus_jitter() {
  // randomValue % (2 * 5000 + 1) == 10000 -> offset +5000 ms
  TEST_ASSERT_EQUAL_UINT64(sleepMicros(5) + 5000ULL * 1000ULL, jitteredSleepMicros(5, 5000, 10000));
}

void test_jitteredSleepMicros_stays_within_bounds_for_any_random_value() {
  const uint32_t samples[] = {1u, 4999u, 5000u, 5001u, 123456789u, 0x7FFFFFFFu, 0xFFFFFFFFu};
  for (uint32_t r : samples) {
    uint64_t us = jitteredSleepMicros(5, 5000, r);
    TEST_ASSERT_GREATER_OR_EQUAL(sleepMicros(5) - 5000ULL * 1000ULL, us);
    TEST_ASSERT_LESS_OR_EQUAL(sleepMicros(5) + 5000ULL * 1000ULL, us);
  }
}

void test_jitteredSleepMicros_never_below_one_second() {
  TEST_ASSERT_EQUAL_UINT64(1000000ULL, jitteredSleepMicros(0, 5000, 0));
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

// ---------- encodeFrame (binary frame v1) ----------
// The hex vectors are copied from loragateway's test_parser.cpp, where the
// decoder is tested against them. Change both repos together.

static std::string hexOf(const uint8_t* data, int len) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  for (int i = 0; i < len; i++) {
    out.push_back(kHex[data[i] >> 4]);
    out.push_back(kHex[data[i] & 0x0F]);
  }
  return out;
}

static std::string stripSpaces(const char* s) {
  std::string out;
  for (; *s; ++s) {
    if (*s != ' ') out.push_back(*s);
  }
  return out;
}

void test_encodeFrame_matches_gateway_healthy_vector() {
  uint8_t buf[64];
  SensorReading r{21.5f, 55.2f, true};
  int len = encodeFrame(buf, sizeof(buf), "GarageTemp", r, 3.90f, 3.5f, 7, 4, nullptr);
  TEST_ASSERT_EQUAL(24, len);
  TEST_ASSERT_EQUAL_STRING(
      stripSpaces("C1 00 0A 47617261676554656D70 070000 040000 D700 2802 BE").c_str(),
      hexOf(buf, len).c_str());
}

void test_encodeFrame_matches_gateway_dht_fail_low_bat_sw_vector() {
  uint8_t buf[64];
  SensorReading r{0.0f, 0.0f, false};
  int len = encodeFrame(buf, sizeof(buf), "ToolroomTemp", r, 3.25f, 3.5f, 1, 1, "49a521c");
  TEST_ASSERT_EQUAL(34, len);
  TEST_ASSERT_EQUAL_STRING(
      stripSpaces("C1 07 0C 546F6F6C726F6F6D54656D70 010000 010000 0080 FFFF 7D 07 34396135323163")
          .c_str(),
      hexOf(buf, len).c_str());
}

void test_encodeFrame_matches_gateway_negative_temp_and_24bit_vector() {
  uint8_t buf[64];
  SensorReading r{-12.5f, 0.0f, true};
  int len = encodeFrame(buf, sizeof(buf), "n", r, 2.0f, 1.0f, 0xABCDEF, 0xFFFFFF, nullptr);
  TEST_ASSERT_EQUAL_STRING(stripSpaces("C1 00 01 6E EFCDAB FFFFFF 83FF 0000 00").c_str(),
                           hexOf(buf, len).c_str());
}

void test_encodeFrame_counters_wrap_to_24_bits() {
  uint8_t buf[64];
  SensorReading r{20.0f, 40.0f, true};
  int len = encodeFrame(buf, sizeof(buf), "n", r, 4.0f, 3.5f, 0x01000005, 0x01000006, nullptr);
  TEST_ASSERT_EQUAL(15, len);
  TEST_ASSERT_EQUAL(0x05, buf[4]);
  TEST_ASSERT_EQUAL(0x00, buf[6]);
  TEST_ASSERT_EQUAL(0x06, buf[7]);
  TEST_ASSERT_EQUAL(0x00, buf[9]);
}

void test_encodeFrame_clamps_battery_voltage_to_byte_range() {
  uint8_t buf[64];
  SensorReading r{20.0f, 40.0f, true};
  encodeFrame(buf, sizeof(buf), "n", r, 5.0f, 3.5f, 1, 1, nullptr);
  TEST_ASSERT_EQUAL(255, buf[14]);  // 4.55 V, the top of the range
  encodeFrame(buf, sizeof(buf), "n", r, 0.0f, 3.5f, 1, 1, nullptr);
  TEST_ASSERT_EQUAL(0, buf[14]);  // 2.00 V, the bottom
}

void test_encodeFrame_never_emits_null_marker_for_a_real_temperature() {
  uint8_t buf[64];
  SensorReading r{-5000.0f, 40.0f, true};  // absurd, but must not read as null
  encodeFrame(buf, sizeof(buf), "n", r, 4.0f, 3.5f, 1, 1, nullptr);
  int16_t t = static_cast<int16_t>(buf[10] | (buf[11] << 8));
  TEST_ASSERT_TRUE(t != INT16_MIN);
}

void test_encodeFrame_rejects_buffer_too_small() {
  uint8_t buf[23];
  SensorReading r{21.5f, 55.2f, true};
  TEST_ASSERT_EQUAL(-1, encodeFrame(buf, sizeof(buf), "GarageTemp", r, 3.9f, 3.5f, 7, 4, nullptr));
}

void test_encodeFrame_rejects_empty_or_too_long_id() {
  uint8_t buf[300];
  SensorReading r{21.5f, 55.2f, true};
  TEST_ASSERT_EQUAL(-1, encodeFrame(buf, sizeof(buf), "", r, 3.9f, 3.5f, 7, 4, nullptr));
  std::string longId(256, 'x');
  TEST_ASSERT_EQUAL(-1, encodeFrame(buf, sizeof(buf), longId.c_str(), r, 3.9f, 3.5f, 7, 4, nullptr));
}

void test_encodeFrame_empty_sw_is_omitted() {
  uint8_t buf[64];
  SensorReading r{21.5f, 55.2f, true};
  int len = encodeFrame(buf, sizeof(buf), "GarageTemp", r, 3.9f, 3.5f, 7, 4, "");
  TEST_ASSERT_EQUAL(24, len);
  TEST_ASSERT_EQUAL(0, buf[1] & 0x04);
}

// ---------- shouldReportVersion ----------
// The gateway never acknowledges, so "sw" is repeated instead: on every send
// until one succeeds, on the first 3 sends after power-up, and once a day.

void test_shouldReportVersion_until_first_success() {
  TEST_ASSERT_TRUE(shouldReportVersion(false, 50, 5));
}

void test_shouldReportVersion_first_three_sends() {
  TEST_ASSERT_TRUE(shouldReportVersion(true, 1, 5));
  TEST_ASSERT_TRUE(shouldReportVersion(true, 3, 5));
  TEST_ASSERT_FALSE(shouldReportVersion(true, 4, 5));
}

void test_shouldReportVersion_daily() {
  // 5-minute wakes: 288 sends a day.
  TEST_ASSERT_TRUE(shouldReportVersion(true, 288, 5));
  TEST_ASSERT_TRUE(shouldReportVersion(true, 576, 5));
  TEST_ASSERT_FALSE(shouldReportVersion(true, 287, 5));
  TEST_ASSERT_FALSE(shouldReportVersion(true, 289, 5));
}

void test_shouldReportVersion_daily_with_zero_sleep_does_not_divide_by_zero() {
  TEST_ASSERT_FALSE(shouldReportVersion(true, 10, 0));
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

  RUN_TEST(test_encodeFrame_matches_gateway_healthy_vector);
  RUN_TEST(test_encodeFrame_matches_gateway_dht_fail_low_bat_sw_vector);
  RUN_TEST(test_encodeFrame_matches_gateway_negative_temp_and_24bit_vector);
  RUN_TEST(test_encodeFrame_counters_wrap_to_24_bits);
  RUN_TEST(test_encodeFrame_clamps_battery_voltage_to_byte_range);
  RUN_TEST(test_encodeFrame_never_emits_null_marker_for_a_real_temperature);
  RUN_TEST(test_encodeFrame_rejects_buffer_too_small);
  RUN_TEST(test_encodeFrame_rejects_empty_or_too_long_id);
  RUN_TEST(test_encodeFrame_empty_sw_is_omitted);
  RUN_TEST(test_shouldReportVersion_until_first_success);
  RUN_TEST(test_shouldReportVersion_first_three_sends);
  RUN_TEST(test_shouldReportVersion_daily);
  RUN_TEST(test_shouldReportVersion_daily_with_zero_sleep_does_not_divide_by_zero);
  RUN_TEST(test_jitteredSleepMicros_without_jitter_is_exact);
  RUN_TEST(test_jitteredSleepMicros_lowest_random_value_gives_minus_jitter);
  RUN_TEST(test_jitteredSleepMicros_spans_plus_jitter);
  RUN_TEST(test_jitteredSleepMicros_stays_within_bounds_for_any_random_value);
  RUN_TEST(test_jitteredSleepMicros_never_below_one_second);

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
