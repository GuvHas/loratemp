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
  uint32_t txCount = 0;

  NodeOutcome outcome = runNode(cfg, /*bootCount=*/2, txCount, sensor, radio, display, power, clock);

  TEST_ASSERT_TRUE(outcome.dhtOk);
  TEST_ASSERT_EQUAL(1, sensor.readCalls);
  // Only the initial settle delay — no DHT retry delay, no LoRa retry delay.
  TEST_ASSERT_EQUAL(1, (int)clock.delaysMs.size());
  TEST_ASSERT_EQUAL_UINT32(2000, clock.delaysMs[0]);
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
  uint32_t txCount = 0;

  NodeOutcome outcome = runNode(cfg, 2, txCount, sensor, radio, display, power, clock);

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
  uint32_t txCount = 0;

  NodeOutcome outcome = runNode(cfg, 2, txCount, sensor, radio, display, power, clock);

  TEST_ASSERT_FALSE(outcome.dhtOk);
  TEST_ASSERT_EQUAL(cfg.dhtMaxRetries, sensor.readCalls);
  TEST_ASSERT_NOT_NULL(strstr(outcome.payload, "\"t\":null"));
  TEST_ASSERT_NOT_NULL(strstr(outcome.payload, "\"err\":\"dht\""));
  // DHT failure forces the display on even on a non-scheduled boot.
  TEST_ASSERT_TRUE(outcome.displayed);
  TEST_ASSERT_EQUAL(1, display.initCalls);
  TEST_ASSERT_EQUAL(1, display.showReadingCalls);
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
  uint32_t txCount = 0;

  NodeOutcome outcome = runNode(cfg, 2, txCount, sensor, radio, display, power, clock);

  TEST_ASSERT_FALSE(outcome.loraOk);
  TEST_ASSERT_EQUAL(0, radio.sendCalls);
  TEST_ASSERT_EQUAL('\0', outcome.payload[0]);
  TEST_ASSERT_EQUAL_UINT32(0, txCount);  // never incremented; no send was attempted
  TEST_ASSERT_FALSE(outcome.displayed);
  TEST_ASSERT_EQUAL(0, display.initCalls);
  // Sleep fallback still runs even though init failed.
  TEST_ASSERT_EQUAL(1, clock.sleepCalls);
  TEST_ASSERT_EQUAL(1, radio.endCalls);
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
  uint32_t txCount = 0;

  NodeOutcome outcome = runNode(cfg, 2, txCount, sensor, radio, display, power, clock);

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
  uint32_t txCount = 0;

  NodeOutcome outcome = runNode(cfg, 2, txCount, sensor, radio, display, power, clock);

  TEST_ASSERT_FALSE(outcome.loraOk);
  TEST_ASSERT_EQUAL(cfg.loraMaxRetries, radio.sendCalls);
  // TX failure forces the display on even on a non-scheduled boot.
  TEST_ASSERT_TRUE(outcome.displayed);
  TEST_ASSERT_FALSE(display.lastSent);
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
  uint32_t txCount = 0;

  NodeOutcome outcome = runNode(cfg, /*bootCount=*/2, txCount, sensor, radio, display, power, clock);

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
  uint32_t txCount = 0;

  // bootCount == displayEveryN -> scheduled boot, everything healthy.
  NodeOutcome outcome = runNode(cfg, cfg.displayEveryN, txCount, sensor, radio, display, power, clock);

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
  uint32_t txCount = 0;

  NodeOutcome outcome = runNode(cfg, 2, txCount, sensor, radio, display, power, clock);

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
  uint32_t txCount = 0;

  runNode(cfg, 2, txCount, sensor, radio, display, power, clock);

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
  uint32_t txCount = 0;

  runNode(cfg, 2, txCount, sensor, radio, display, power, clock);

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
  uint32_t txCount = 0;

  runNode(cfg, 2, txCount, sensor, radio, display, power, clock);

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
  uint32_t txCount = 0;

  runNode(cfg, 2, txCount, sensor, radio, display, power, clock);

  TEST_ASSERT_EQUAL(1, clock.sleepCalls);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_dht_succeeds_on_first_attempt);
  RUN_TEST(test_dht_retries_then_succeeds);
  RUN_TEST(test_dht_exhausts_all_retries);

  RUN_TEST(test_lora_begin_failure_aborts_before_building_or_sending_payload);
  RUN_TEST(test_lora_send_retries_then_succeeds);
  RUN_TEST(test_lora_send_fails_after_all_retries);

  RUN_TEST(test_display_stays_off_when_healthy_and_not_scheduled);
  RUN_TEST(test_display_shown_on_scheduled_boot_calls_init_once);

  RUN_TEST(test_low_battery_forces_display_and_sets_flag);

  RUN_TEST(test_sleep_called_exactly_once_on_success_path);
  RUN_TEST(test_sleep_called_exactly_once_when_dht_fails);
  RUN_TEST(test_sleep_called_exactly_once_when_lora_send_fails);
  RUN_TEST(test_sleep_called_exactly_once_when_lora_init_fails);

  return UNITY_END();
}
