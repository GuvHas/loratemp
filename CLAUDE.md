# loratemp

Firmware for a battery-powered sensor node (LilyGo TTGO T3 V1.6.1). Each wake-up
it reads a DHT22, sends one JSON packet over LoRa and goes back to deep sleep.
The receiving side is [GuvHas/loragateway](https://github.com/GuvHas/loragateway).

## Commands

```sh
pio test -e native            # unit tests, no hardware needed
pio run -e ttgo-lora32-v21    # firmware build
```

CI runs both on every push and pull request. Run both before pushing.

The ESP32 platform is pinned to 8.0.0 (Arduino core 3.x, ESP-IDF 5) in
`platformio.ini`, the same version as loragateway. Before that it was unpinned,
so the version depended on what the building machine had installed. A platform
bump is its own change, and the owner tests it on the board.

Four paths under `.pio/` are tracked, and every build modifies some of them.
Stage files by name and leave `.pio/` out of commits.

## Layout

| Path | What it holds |
| --- | --- |
| `include/payload.h`, `src/payload.cpp` | Pure functions: `formatPayload()`, sleep duration, display scheduling. No Arduino headers. |
| `include/hal.h` | Interfaces for sensor, radio, display, power, clock and logger. |
| `include/hal_esp32.h`, `src/hal_esp32.cpp` | ESP32 implementations of those interfaces. |
| `include/orchestrator.h`, `src/orchestrator.cpp` | `runNode()`: one full wake cycle. Uses only `hal.h`. |
| `src/main.cpp` | Per-node configuration, pin map, RTC-persisted counters, wiring. |
| `scripts/inject_git_version.py` | Injects the git short hash as `NODE_FW_VERSION`. |
| `test/test_payload`, `test/test_orchestrator` | Unity tests. `fakes.h` has fakes for every `hal.h` interface. |

The `native` environment does not compile `main.cpp` or `hal_esp32.cpp`, so
logic that needs a test belongs in `payload` or `orchestrator`.

## How changes are made

1. Branch from `main`.
2. Write or update the native test first, then implement.
3. Run both commands above.
4. Open a pull request against `main`. Codex reviews it automatically. Treat
   each finding as a bug report: check it, fix it with a test, reply and
   resolve the thread.
5. The owner merges, or asks for the merge, once CI is green.

Nothing here can be verified on real hardware from this repository. Say so in
the pull request and leave it as a manual check for the owner.

## Constraints that shape every change

- **No WiFi and no OTA.** WiFi and Bluetooth are switched off to save battery.
  New firmware reaches a node only when someone connects a cable to it.
- **Every code path ends in deep sleep.** `runNode()` calls
  `clock.deepSleep()` on success, sensor failure, radio failure and radio
  init failure. A path that returns without sleeping drains the battery.
- **Airtime and wake time cost battery.** Do not add payload bytes, retries or
  delays without a reason that outweighs that cost.

## One wake cycle

`setup()` increments `bootCount`, builds the adapters and calls `runNode()`:

1. Read the DHT22, up to 3 attempts.
2. Read the battery voltage.
3. Start the radio. If that fails, sleep without sending.
4. Increment `txCount`, build the payload, send it, up to 2 attempts.
5. Show the reading on the display on the first boot, every 12th boot, or
   when the sensor, the radio or the battery needs attention.
6. Deep sleep for `SLEEP_MINUTES`.

`NodeId`, the sleep interval and the radio settings are constants at the top of
`src/main.cpp`. `NodeId` is set per device before flashing.

## State that survives deep sleep

`bootCount`, `txCount` and `versionReported` are `RTC_DATA_ATTR`. They survive
deep sleep and reset only when the node loses power or is flashed.

`bootCount` is incremented before it is used, so the first packet after a power
loss carries `boot: 1`. The gateway depends on this: it treats `boot == 1` as a
cold boot and resets its duplicate filter for the node. Do not change the
increment order without changing the gateway first.

## Payload contract with loragateway

```json
{"id":"GarageTemp","t":21.5,"h":55.2,"v":3.90,"boot":7,"seq":4,"lb":0,"err":"none","sw":"fde884a"}
```

- `t` and `h` are `null` and `err` is `"dht"` when the sensor read failed.
- `seq` is `txCount`. `lb` is 1 when the battery is below `LOW_BAT_VOLTAGE`.
- `sw` is `NODE_FW_VERSION`. `runNode()` includes it while `versionReported`
  is false and sets the flag only after a send carrying it succeeds. A failed
  radio init or failed send leaves the flag false, so the next wake tries
  again.
- `formatPayload()` falls back to a short `"err":"fmt"` payload if the full one
  does not fit, and to a fixed literal if that does not fit either. The buffer
  is 192 bytes and a LoRa packet holds at most 255.

The gateway rejects a payload whose known fields have the wrong type and
ignores fields it does not know. Add fields gateway-first.

Radio settings must match the gateway: 868 MHz, spreading factor 9, CRC on.

## Known gaps

- A successful send means the radio finished transmitting. The gateway does
  not acknowledge packets, so `sw` is lost if the gateway was off or out of
  range for that one packet, and is not sent again until the next power loss.
