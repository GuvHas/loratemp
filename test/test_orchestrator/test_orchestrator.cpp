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
  // Only the initial settle wait — no DHT retry wait, no LoRa retry delay.
  // The settle wait is a low-power idle (light sleep on hardware), never a
  // busy delay: the DHT22 only needs time, not the CPU.
  TEST_ASSERT_EQUAL(0, (int)clock.delaysMs.size());
  TEST_ASSERT_EQUAL(1, (int)clock.idlesMs.size());
  TEST_ASSERT_EQUAL_UINT32(2000, clock.idlesMs[0]);
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
  // Settle wait + 2 inter-attempt retry waits, all 2000ms and all
  // low-power idles rather than busy delays.
  TEST_ASSERT_EQUAL(0, (int)clock.delaysMs.size());
  TEST_ASSERT_EQUAL(3, (int)clock.idlesMs.size());
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_EQUAL_UINT32(2000, clock.idlesMs[i]);
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
  // One inter-retry LoRa delay (100); the settle wait is an idle.
  TEST_ASSERT_EQUAL(1, (int)clock.delaysMs.size());
  TEST_ASSERT_EQUAL_UINT32(100, clock.delaysMs[0]);
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
  uint32_t txCount = 10;  // past the first 3 sends, not a daily repeat
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

// The gateway never acknowledges, so a version that was "sent" may still
// have been lost. It is repeated on the first 3 sends after power-up and
// once a day, even after a send carrying it succeeded.
void test_sw_repeated_on_first_three_sends_even_when_reported() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 2;  // this send is number 3
  bool versionReported = true;

  NodeOutcome outcome =
      runNode(cfg, 3, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_NOT_NULL(strstr(outcome.payload, "\"sw\""));
}

void test_sw_repeated_once_a_day() {
  NodeConfig cfg = defaultTestConfig();  // 5-minute sleep: 288 sends a day
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 287;
  bool versionReported = true;

  NodeOutcome outcome =
      runNode(cfg, 300, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_NOT_NULL(strstr(outcome.payload, "\"sw\""));
}

// ---------- Binary frame ----------

void test_binary_mode_sends_frame_bytes() {
  NodeConfig cfg = defaultTestConfig();
  cfg.binaryPayload = true;
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 10;
  bool versionReported = true;

  NodeOutcome outcome =
      runNode(cfg, 12, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_TRUE(outcome.loraOk);
  TEST_ASSERT_EQUAL(1, (int)radio.sentPayloads.size());
  const std::string& frame = radio.sentPayloads[0];
  // magic + flags + len + "TestNode" + 11 fixed bytes, no sw
  TEST_ASSERT_EQUAL(3 + 8 + 11, (int)frame.size());
  TEST_ASSERT_EQUAL(0xC1, (uint8_t)frame[0]);
  TEST_ASSERT_EQUAL(0x00, (uint8_t)frame[1]);
  TEST_ASSERT_EQUAL(11, (uint8_t)frame[14]);  // seq low byte, after id
  // The log shows the frame as hex, since the bytes are not printable.
  TEST_ASSERT_TRUE(logger.messages[0].rfind("Sending: frame C1000854657374", 0) == 0);
}

void test_binary_mode_includes_sw_and_marks_it_reported() {
  NodeConfig cfg = defaultTestConfig();
  cfg.binaryPayload = true;
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

  runNode(cfg, 1, txCount, versionReported, sensor, radio, display, power, clock, logger);

  const std::string& frame = radio.sentPayloads[0];
  TEST_ASSERT_EQUAL(0x04, (uint8_t)frame[1] & 0x04);
  TEST_ASSERT_EQUAL(3 + 8 + 11 + 1 + 3, (int)frame.size());  // sw "dev" in native tests
  TEST_ASSERT_TRUE(versionReported);
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

// Holding the reading on the OLED needs time, not the CPU: the panel keeps
// its own framebuffer, so the hold is a low-power idle.
void test_display_hold_is_low_power_idle() {
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

  runNode(cfg, cfg.displayEveryN, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_EQUAL(0, (int)clock.delaysMs.size());
  TEST_ASSERT_EQUAL(2, (int)clock.idlesMs.size());  // settle, then display hold
  TEST_ASSERT_EQUAL_UINT32(cfg.displaySeconds * 1000, clock.idlesMs[1]);
}

// ---------- Low battery ----------

// A low battery used to force the OLED on every wake, which kept the node
// awake ~2 s longer on every cycle exactly when the battery could least
// afford it. The "lb" flag still reaches the gateway every wake, and the
// scheduled display boots still show the warning.
void test_low_battery_sets_flag_without_forcing_display() {
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

  TEST_ASSERT_FALSE(outcome.displayed);
  TEST_ASSERT_EQUAL(0, display.initCalls);
  TEST_ASSERT_NOT_NULL(strstr(outcome.payload, "\"lb\":1"));
}

void test_low_battery_shown_on_scheduled_display_boot() {
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

  NodeOutcome outcome = runNode(cfg, cfg.displayEveryN, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_TRUE(outcome.displayed);
  TEST_ASSERT_TRUE(display.lastLowBat);
}

// ---------- Critical battery ----------
// Below criticalBatVoltage the node still sends (so Home Assistant sees the
// last reading and lb:1) but then sleeps criticalSleepMinutes instead of
// sleepMinutes, to stop draining a nearly empty cell.

void test_critical_battery_sends_then_sleeps_long() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  power.voltage = 3.1f;  // below cfg.criticalBatVoltage (3.2)
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  NodeOutcome outcome = runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_TRUE(outcome.loraOk);
  TEST_ASSERT_NOT_NULL(strstr(outcome.payload, "\"lb\":1"));
  TEST_ASSERT_EQUAL(1, clock.sleepCalls);
  TEST_ASSERT_EQUAL_UINT64(sleepMicros(cfg.criticalSleepMinutes), clock.sleptMicros);
}

void test_critical_battery_sleeps_long_even_when_radio_init_fails() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.beginResult = false;
  FakeDisplay display;
  FakePower power;
  power.voltage = 3.1f;
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_EQUAL(1, clock.sleepCalls);
  TEST_ASSERT_EQUAL_UINT64(sleepMicros(cfg.criticalSleepMinutes), clock.sleptMicros);
}

void test_low_but_not_critical_battery_keeps_normal_sleep() {
  NodeConfig cfg = defaultTestConfig();
  FakeSensor sensor;
  sensor.queuedReads = {{21.5f, 55.2f, true}};
  FakeRadio radio;
  radio.queuedSendResults = {true};
  FakeDisplay display;
  FakePower power;
  power.voltage = 3.25f;  // below lowBatVoltage (3.3), above critical (3.2)
  FakeClock clock;
  FakeLogger logger;
  uint32_t txCount = 0;
  bool versionReported = true;  // already reported; this test is not about that

  runNode(cfg, 2, txCount, versionReported, sensor, radio, display, power, clock, logger);

  TEST_ASSERT_EQUAL_UINT64(sleepMicros(cfg.sleepMinutes), clock.sleptMicros);
}

// ---------- Sleep jitter ----------

void test_sleep_duration_includes_jitter_from_random_value() {
  NodeConfig cfg = defaultTestConfig();
  cfg.sleepJitterMs = 5000;
  cfg.randomValue = 0;  // lowest value -> -5000 ms
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

  TEST_ASSERT_EQUAL_UINT64(sleepMicros(cfg.sleepMinutes) - 5000ULL * 1000ULL, clock.sleptMicros);
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
  RUN_TEST(test_sw_repeated_on_first_three_sends_even_when_reported);
  RUN_TEST(test_sw_repeated_once_a_day);
  RUN_TEST(test_binary_mode_sends_frame_bytes);
  RUN_TEST(test_binary_mode_includes_sw_and_marks_it_reported);
  RUN_TEST(test_version_reported_flag_unchanged_when_lora_init_fails);
  RUN_TEST(test_version_reported_flag_unchanged_when_all_send_attempts_fail);

  RUN_TEST(test_display_stays_off_when_healthy_and_not_scheduled);
  RUN_TEST(test_display_shown_on_scheduled_boot_calls_init_once);

  RUN_TEST(test_display_hold_is_low_power_idle);
  RUN_TEST(test_low_battery_sets_flag_without_forcing_display);
  RUN_TEST(test_low_battery_shown_on_scheduled_display_boot);

  RUN_TEST(test_critical_battery_sends_then_sleeps_long);
  RUN_TEST(test_critical_battery_sleeps_long_even_when_radio_init_fails);
  RUN_TEST(test_low_but_not_critical_battery_keeps_normal_sleep);
  RUN_TEST(test_sleep_duration_includes_jitter_from_random_value);
  RUN_TEST(test_sleep_called_exactly_once_on_success_path);
  RUN_TEST(test_sleep_called_exactly_once_when_dht_fails);
  RUN_TEST(test_sleep_called_exactly_once_when_lora_send_fails);
  RUN_TEST(test_sleep_called_exactly_once_when_lora_init_fails);

  return UNITY_END();
}
