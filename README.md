# loratemp

Firmware for a battery-powered LoRa temperature and humidity node. Each wake-up
it reads a DHT22, sends one packet over LoRa and goes back to deep sleep. The
readings reach Home Assistant through the companion gateway,
[GuvHas/loragateway](https://github.com/GuvHas/loragateway).

## Hardware

- LilyGo TTGO T3 V1.6.1 LoRa board (868 MHz)
- DHT22 on GPIO 13
- SSD1306 OLED on I2C, SDA 21 and SCL 22
- Green LED on GPIO 25, lit while a packet is being sent
- Battery voltage read on GPIO 35 through the board's divider

WiFi and Bluetooth are switched off to save battery, so there is no OTA:
new firmware reaches a node over USB.

## What a node does

1. Wakes from deep sleep (every 5 minutes by default, offset by up to ±5 s of
   random jitter so nodes powered up together drift apart instead of
   transmitting at the same moment).
2. Reads the DHT22, with one retry. The settle time and the retry wait are
   spent in light sleep rather than a busy loop.
3. Reads the calibrated battery voltage.
4. Sends one packet, with one retry. Each send has a TX-done timeout, so a
   stuck radio can't keep the node awake.
5. Shows the reading on the OLED on the first boot, every 12th boot (about
   once an hour), or when the sensor or radio failed.
6. Goes back to deep sleep.

Every wake is also bounded by a 30 s watchdog. If anything hangs, the chip
restarts and the next packet carries `boot: 1`, which the gateway treats as a
fresh start.

### Battery handling

- **Low battery** (`lb: 1`, shown as "Low Battery" in Home Assistant) below
  3.5 V on the calibrated reading.
- **Critical battery** below 3.3 V: the node still sends its reading, then
  sleeps 60 minutes instead of 5 to protect a nearly empty LiPo.

## Packet format

By default (`BINARY_PAYLOAD = true`) the node sends a compact binary frame:
24 bytes for a typical reading instead of about 83 bytes of JSON, which cuts
airtime at SF9 from about 472 ms to 206 ms. This needs loragateway #29 or
later. Set `BINARY_PAYLOAD = false` to send JSON instead:

```json
{"id":"GarageTemp","t":21.5,"h":55.2,"v":3.90,"boot":7,"seq":4,"lb":0,"err":"none","sw":"fde884a"}
```

Both formats carry the same fields. `sw` (the firmware's git short hash) is
not in every packet: it is sent on the first 3 sends after power-up, until one
send carrying it succeeds, and once a day after that. The binary layout is
documented in loragateway's CLAUDE.md ("Binary frame v1").

Radio settings must match the gateway: 868 MHz, spreading factor 9, CRC on.

## In Home Assistant

Once the node is approved on the gateway's `http://<gateway-ip>/devices` page,
it shows up as its own device with Temperature, Humidity, Battery, Low Battery
and Signal sensors, plus diagnostics for Boot Count, Sequence, Error, SNR and
Packet Loss.

<!--
  SCREENSHOT PLACEHOLDER: insert a new screenshot of this node's device page
  in Home Assistant here (sensors plus diagnostics, including SNR and Packet
  Loss). Upload the image by dragging it into a GitHub comment or edit box,
  then replace the line below with the generated <img> tag or an
  ![...](...) link.

  ![Home Assistant sensors for a loratemp node](docs/ha-node-sensors.png)
-->

## Download firmware

Every push to `main` builds the firmware and publishes it to the
[**firmware-latest** release](https://github.com/GuvHas/loratemp/releases/tag/firmware-latest).
The links work without a GitHub account.

| File | Use it for | Flash at |
| --- | --- | --- |
| [`firmware.bin`](https://github.com/GuvHas/loratemp/releases/download/firmware-latest/firmware.bin) | Updating a node that already runs this project | `0x10000` |
| [`firmware-full.bin`](https://github.com/GuvHas/loratemp/releases/download/firmware-latest/firmware-full.bin) | A blank or erased board: bootloader, partition table and app in one image | `0x0` |

```sh
esptool.py --chip esp32 write_flash 0x10000 firmware.bin       # existing node
esptool.py --chip esp32 write_flash 0x0 firmware-full.bin      # new board
```

Without installing anything, Espressif's browser flasher
[esptool-js](https://espressif.github.io/esptool-js/) (Chrome or Edge) can
write either file: connect, pick the file and enter its offset.

> **The prebuilt firmware uses the default settings**, including
> `NodeId = "GarageTemp"`. Every node needs its own `NodeId`, so for a
> second node, or any other setting, change `src/main.cpp` and build it
> yourself (see "Building and testing").

Each pull request builds the same files as a workflow artifact, on the run's
page under **Artifacts** (needs a GitHub sign-in).

## Configuration

Settings are constants at the top of `src/main.cpp`. Set `NodeId` per device
before flashing.

| Constant | Default | Meaning |
| --- | --- | --- |
| `NodeId` | `"GarageTemp"` | Node name, used as the device name in Home Assistant |
| `SLEEP_MINUTES` | 5 | Time between readings |
| `SLEEP_JITTER_MS` | 5000 | Random offset applied to each sleep, ± |
| `LOW_BAT_VOLTAGE` | 3.5 | Below this the packet carries `lb: 1` |
| `CRITICAL_BAT_VOLTAGE` | 3.3 | Below this the node sleeps `CRITICAL_SLEEP_MINUTES` |
| `CRITICAL_SLEEP_MINUTES` | 60 | Sleep time on a critical battery |
| `BINARY_PAYLOAD` | `true` | Binary frame (`true`) or JSON (`false`) |
| `LORA_SF` | 9 | Spreading factor, must match the gateway |
| `LORA_TX_POWER` | 14 | TX power in dBm |
| `DISPLAY_EVERY_N` | 12 | Show the OLED every Nth boot |
| `DISPLAY_SECONDS` | 2 | How long the OLED stays on |
| `LIGHT_SLEEP_WAITS` | `true` | Spend sensor and display waits in light sleep; set `false` if the sensor or display misbehaves |
| `CPU_MHZ` | 80 | CPU frequency |

## Building and testing

This is a [PlatformIO](https://platformio.org/) project. The ESP32 platform is
pinned to `espressif32 @ 8.0.0` (Arduino core 3.x, ESP-IDF 5), the same
version as loragateway.

```sh
pio test -e native            # unit tests, no hardware needed
pio run -e ttgo-lora32-v21    # firmware build
pio run -e ttgo-lora32-v21 -t upload
```

CI runs the tests and the firmware build on every push and pull request.
[CLAUDE.md](CLAUDE.md) describes the code layout, the wake cycle and the
payload contract with the gateway in more detail.

## Troubleshooting (PlatformIO)

If you see an error like `ModuleNotFoundError: No module named "intelhex"` while generating `bootloader.bin`, the pre-build script in this repo attempts to install it automatically.

If your local PlatformIO environment is still broken, run:

```bash
pio system prune --force
pio pkg update --global
```

Then retry the build.
