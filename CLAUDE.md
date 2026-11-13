# loratemp

Firmware for a battery-powered sensor node (LilyGo TTGO T3 V1.6.1). Each wake-up
it reads a DHT22, sends one packet over LoRa (a binary frame, or JSON) and goes
back to deep sleep.
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
| `include/payload.h`, `src/payload.cpp` | Pure functions: `formatPayload()` (JSON), `encodeFrame()` (binary), `resolveNodeId()`, sleep duration, display scheduling. No Arduino headers. |
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
- **Every wake is bounded by the task watchdog** (`WAKE_WATCHDOG_S`, 30 s,
  never fed). A hang restarts the chip; RTC memory is then re-initialised, so
  the next packet carries `boot: 1`. A change that makes a normal wake longer
  must stay well inside that timeout.
- **Every code path ends in deep sleep.** `runNode()` calls
  `clock.deepSleep()` on success, sensor failure, radio failure and radio
  init failure. A path that returns without sleeping drains the battery.
- **Airtime and wake time cost battery.** Do not add payload bytes, retries or
  delays without a reason that outweighs that cost.

## One wake cycle

`setup()` increments `bootCount`, builds the adapters and calls `runNode()`:

1. Wait `dhtSettleDelayMs` for the DHT22 (it is not always powered), then
   read it, up to 2 attempts 2 s apart. Waits that need time but not the
   CPU (the settle, DHT retries, the OLED hold) use `IClock::idleMs()`, which
   is light sleep on hardware. Short waits around the radio use `delayMs()`.
2. Read the battery voltage.
3. Start the radio. If that fails, sleep without sending.
4. Increment `txCount`, build the payload, send it, up to 2 attempts. Each
   attempt waits for the radio's TX-done interrupt for at most
   `loraTxTimeoutMs()` (twice the airtime plus 100 ms) and counts as failed
   if it does not arrive. The library's blocking `endPacket()` has no timeout,
   so do not go back to it.
5. Show the reading on the display on the first boot, every 12th boot, or
   when the sensor or the radio failed. A low battery does not force it: that
   would lengthen every wake when the battery can least afford it.
6. Deep sleep for `SLEEP_MINUTES`, offset by a random amount up to
   `SLEEP_JITTER_MS` either way, or for `CRITICAL_SLEEP_MINUTES` when the
   battery is below `CRITICAL_BAT_VOLTAGE`. The packet is still sent first.

`NodeId`, the sleep interval and the radio settings are constants at the top of
`src/main.cpp`. `NodeId` defaults to empty (or the `NODE_ID` build flag), and
`resolveNodeId()` then derives the id from the MAC: `n-` plus the last 3 MAC
bytes in hex, 8 bytes in total. A fixed id still wins, so a node can keep an
id that already has history in Home Assistant. The readable name lives on
the gateway (`/devices`), not on the node.

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

- `id` is the resolved node id, `n-xxxxxx` unless a fixed `NodeId` is set.
- `t` and `h` are `null` and `err` is `"dht"` when the sensor read failed.
- `seq` is `txCount`. `lb` is 1 when the battery is below `LOW_BAT_VOLTAGE`.
- `sw` is `NODE_FW_VERSION`. `shouldReportVersion()` decides when it goes
  out: on every send while `versionReported` is false (the flag is set only
  after a send carrying it succeeds), on the first 3 sends after power-up, and
  once a day. The gateway does not acknowledge, so the repeats cover a lost
  packet.
- `formatPayload()` falls back to a short `"err":"fmt"` payload if the full one
  does not fit, and to a fixed literal if that does not fit either. The buffer
  is 192 bytes and a LoRa packet holds at most 255.

The gateway rejects a payload whose known fields have the wrong type and
ignores fields it does not know. Add fields gateway-first.

### Binary frame

With `BINARY_PAYLOAD` (the default) the node sends the same fields as a
version-1 binary frame built by `encodeFrame()`: 24 bytes for a typical
reading instead of ~83, about 206 ms on air at SF9 instead of 472 ms. The
layout is defined in loragateway's CLAUDE.md ("Binary frame v1"). The hex
vectors in `test/test_payload/test_payload.cpp` are copied from loragateway's
`test_parser.cpp`; change both together. The gateway accepts both formats,
so `BINARY_PAYLOAD = false` goes back to JSON. `ILoRaRadio::send()` takes
bytes and a length because a frame contains NULs.

Radio settings must match the gateway: 868 MHz, spreading factor 9, CRC on.

## Known gaps

- A successful send means the radio finished transmitting. The gateway does
  not acknowledge packets, so a lost `sw` is only repeated on the first 3
  sends and then once a day.
