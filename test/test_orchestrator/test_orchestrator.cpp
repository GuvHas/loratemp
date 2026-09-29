#include <unity.h>

#include <cstring>

#include "fakes.h"
#include "orchestrator.h"

void setUp() {}
void tearDown() {}

// ---------- DHT read retries ----------

void test_dht_succeeds_on_first_attempt() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  NodeOutcome outcome = runNode(cfg, /*bootCount=*/2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_TRUE(outcome.dhtOk);
  TEST_ASSERT_EQUAL(1, sensor.readCalls);
  // Only the initial settle delay — no DHT retry delay, no LoRa retry delay.
  TEST_ASSERT_EQUAL(1, (int)clock.delaysMs.size());
  TEST_ASSERT_EQUAL_UINT32(2000, clock.delaysMs[0]);
  TEST_ASSERT_EQUAL(1, (int)logger.messages.size());
  TEST_ASSERT_TRUE(logger.messages[0].rfind("Sending: {", 0) == 0);
}

void test_dht_retries_then_succeeds() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{0, 0, false}, {0, 0, false}, {22.0f, 50.0f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  NodeOutcome outcome = runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_TRUE(outcome.dhtOk);
  TEST_ASSERT_EQUAL(3, sensor.readCalls);
  // Settle delay + 2 inter-attempt retry delays, all 2000ms.
  TEST_ASSERT_EQUAL(3, (int)clock.delaysMs.size());
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_EQUAL_UINT32(2000, clock.delaysMs[i]);
  }
}

void test_dht_exhausts_all_retries() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;  // empty queue -> every read() is invalid
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  NodeOutcome outcome = runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_FALSE(outcome.dhtOk);
  TEST_ASSERT_EQUAL(cfg.dhtMaxRetries, sensor.readCalls);
  TEST_ASSERT_NOT_NULL(strstr(outcome.payload, "\"t\":null"));
  TEST_ASSERT_NOT_NULL(strstr(outcome.payload, "\"err\":\"dht\""));
  // DHT failure forces the display on even on a non-scheduled boot.
  TEST_ASSERT_TRUE(outcome.displayed);
  TEST_ASSERT_EQUAL(1, display.initCalls);
  TEST_ASSERT_EQUAL(1, display.showReadingCalls);
  TEST_ASSERT_EQUAL_STRING("DHT Read Failed after retries!", logger.messages.front().c_str());
}

// ---------- LoRa TX failures ----------

void test_lora_begin_failure_aborts_before_building_or_sending_payload() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.beginResult = false;
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  NodeOutcome outcome = runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_FALSE(outcome.loraOk);
  TEST_ASSERT_EQUAL(0, radio.sendCalls);
  TEST_ASSERT_EQUAL('\0', outcome.payload[0]);
  TEST_ASSERT_EQUAL_UINT32(0, txCount);  // never incremented; no send was attempted
  TEST_ASSERT_FALSE(outcome.displayed);
  TEST_ASSERT_EQUAL(0, display.initCalls);
  // Sleep fallback still runs even though init failed.
  TEST_ASSERT_EQUAL(1, clock.sleepCalls);
  TEST_ASSERT_EQUAL(1, radio.endCalls);
  TEST_ASSERT_EQUAL_STRING("LoRa Init Failed!", logger.messages.back().c_str());
}

void test_lora_begin_failure_on_scheduled_boot_reports_display_was_on() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.beginResult = false;
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  // bootCount == displayEveryN -> display.init() already ran before the
  // LoRa init check, so the outcome must reflect that it was shown.
  NodeOutcome outcome = runNode(cfg, cfg.displayEveryN, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_EQUAL(1, display.initCalls);
  TEST_ASSERT_TRUE(outcome.displayed);
  TEST_ASSERT_EQUAL(1, display.offCalls);
}

void test_lora_send_retries_then_succeeds() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {false, true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  NodeOutcome outcome = runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_TRUE(outcome.loraOk);
  TEST_ASSERT_EQUAL(2, radio.sendCalls);
  TEST_ASSERT_EQUAL_UINT32(1, txCount);
  // Settle delay (2000) + one inter-retry LoRa delay (100).
  TEST_ASSERT_EQUAL(2, (int)clock.delaysMs.size());
  TEST_ASSERT_EQUAL_UINT32(100, clock.delaysMs[1]);
}

void test_lora_send_fails_after_all_retries() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {false, false};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  NodeOutcome outcome = runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_FALSE(outcome.loraOk);
  TEST_ASSERT_EQUAL(cfg.loraMaxRetries, radio.sendCalls);
  // TX failure forces the display on even on a non-scheduled boot.
  TEST_ASSERT_TRUE(outcome.displayed);
  TEST_ASSERT_FALSE(display.lastSent);
  TEST_ASSERT_EQUAL_STRING("LoRa TX failed after retries!", logger.messages.back().c_str());
}

// ---------- Cold-boot firmware version reporting ----------
// Codex review (loratemp PR #12): gating "sw" on bootCount == 1 alone meant
// a transient radio failure on that one boot permanently lost the node's
// only opportunity to report its version, since every later wake has
// bootCount > 1. versionReported must instead only flip to true once a send
// that actually carried "sw" succeeds, so a failure keeps retrying on the
// very next wake instead of waiting for another physical power cycle.

void test_version_reported_flag_set_after_successful_send_when_pending() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = false;  // gateway hasn't confirmed receipt yet

  runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_TRUE(versionReported);
}

void test_sw_field_included_in_payload_when_not_yet_reported() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = false;

  NodeOutcome outcome =
      runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_NOT_NULL(strstr(outcome.payload, "\"sw\""));
}

void test_sw_field_omitted_from_payload_when_already_reported() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;

  NodeOutcome outcome =
      runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_NULL(strstr(outcome.payload, "\"sw\""));
  TEST_ASSERT_TRUE(versionReported);  // unchanged
}

// The exact scenario Codex flagged: radio.begin() fails on the boot that
// would have reported the version. formatPayload() is never even called
// (see test_lora_begin_failure_aborts_before_building_or_sending_payload),
// so versionReported must stay false -- otherwise the node would never get
// another chance to report short of a physical power cycle.
void test_version_reported_flag_unchanged_when_lora_init_fails() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.beginResult = false;
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = false;

  runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_FALSE(versionReported);
}

// The other half of Codex's scenario: radio.begin() succeeds and the
// payload (with "sw") is built, but every send attempt fails -- the packet
// carrying "sw" never actually reached the gateway, so this must not be
// treated as reported either.
void test_version_reported_flag_unchanged_when_all_send_attempts_fail() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {false, false};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = false;

  runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_FALSE(versionReported);
}

// ---------- Display scheduling ----------

void test_display_stays_off_when_healthy_and_not_scheduled() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  NodeOutcome outcome = runNode(cfg, /*bootCount=*/2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_FALSE(outcome.displayed);
  TEST_ASSERT_EQUAL(0, display.initCalls);
  TEST_ASSERT_EQUAL(0, display.showReadingCalls);
  TEST_ASSERT_EQUAL(0, display.offCalls);
}

void test_display_shown_on_scheduled_boot_calls_init_once() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  // bootCount == displayEveryN -> scheduled boot, everything healthy.
  NodeOutcome outcome = runNode(cfg, cfg.displayEveryN, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_TRUE(outcome.displayed);
  TEST_ASSERT_EQUAL(1, display.initCalls);  // not re-init'd by the force-display branch
  TEST_ASSERT_EQUAL(1, display.showReadingCalls);
  TEST_ASSERT_EQUAL(1, display.offCalls);
}

// ---------- Low battery ----------

void test_low_battery_forces_display_and_sets_flag() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  power.voltage = 3.0f;  // below cfg.lowBatVoltage (3.3)
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  NodeOutcome outcome = runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_TRUE(outcome.displayed);
  TEST_ASSERT_TRUE(display.lastLowBat);
  TEST_ASSERT_NOT_NULL(strstr(outcome.payload, "\"lb\":1"));
}

// ---------- Sleep fallback: every path must reach deep sleep exactly once ----------

void test_sleep_called_exactly_once_on_success_path() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_EQUAL(1, clock.sleepCalls);
  TEST_ASSERT_EQUAL_UINT64(5ULL * 60ULL * 1000000ULL, clock.sleptMicros);
}

void test_sleep_called_exactly_once_when_dht_fails() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;  // all reads invalid
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_EQUAL(1, clock.sleepCalls);
}

void test_sleep_called_exactly_once_when_lora_send_fails() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {false, false};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_EQUAL(1, clock.sleepCalls);
}

void test_sleep_called_exactly_once_when_lora_init_fails() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.beginResult = false;
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_EQUAL(1, clock.sleepCalls);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_dht_succeeds_on_first_attempt);
  RUN_TEST(test_dht_retries_then_succeeds);
  RUN_TEST(test_dht_exhausts_all_retries);

  RUN_TEST(test_lora_begin_failure_aborts_before_building_or_sending_payload);
  RUN_TEST(test_lora_begin_failure_on_scheduled_boot_reports_display_was_on);
  RUN_TEST(test_lora_send_retries_then_succeeds);
  RUN_TEST(test_lora_send_fails_after_all_retries);

  RUN_TEST(test_version_reported_flag_set_after_successful_send_when_pending);
  RUN_TEST(test_sw_field_included_in_payload_when_not_yet_reported);
  RUN_TEST(test_sw_field_omitted_from_payload_when_already_reported);
  RUN_TEST(test_version_reported_flag_unchanged_when_lora_init_fails);
  RUN_TEST(test_version_reported_flag_unchanged_when_all_send_attempts_fail);

  RUN_TEST(test_display_stays_off_when_healthy_and_not_scheduled);
  RUN_TEST(test_display_shown_on_scheduled_boot_calls_init_once);

  RUN_TEST(test_low_battery_forces_display_and_sets_flag);

  RUN_TEST(test_sleep_called_exactly_once_on_success_path);
  RUN_TEST(test_sleep_called_exactly_once_when_dht_fails);
  RUN_TEST(test_sleep_called_exactly_once_when_lora_send_fails);
  RUN_TEST(test_sleep_called_exactly_once_when_lora_init_fails);

  return UNITY_END();
}
