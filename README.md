# loratemp

An attempt to read values from a DHT22 and send them over LoRa.

Also presenting the values on the screen of a LilyGo TTGO T3_V1.6.1 Lora Board

DHT connected to GPIO 13

SSD1306 using 21, 22

Green LED on the board, using GPIO25, used to signal when transmitting packet

Gateway repo: https://github.com/GuvHas/loragateway

## Troubleshooting (PlatformIO)

If you see an error like `ModuleNotFoundError: No module named "intelhex"` while generating `bootloader.bin`, the pre-build script in this repo now attempts to install it automatically.

If your local PlatformIO environment is still broken, run:

```bash
pio system prune --force
pio pkg update --global
```

Then retry build.

