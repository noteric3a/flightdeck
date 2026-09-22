# ESP32 hardware setup

The default target is a single 64 × 32, 1/16-scan HUB75 RGB panel. Confirm the controller and panel before flashing. An ESP32 chip family does not identify the controller's GPIO wiring.

`platformio.ini` includes generic ESP32 and ESP32-S3 profiles. `include/config.h` owns the pin definitions. The firmware passes them explicitly into the HUB75 driver. If your board already has a HUB75 connector, use its published schematic rather than rewiring to the generic example.

| HUB75 signal | Generic ESP32 GPIO | Generic ESP32-S3 GPIO |
| --- | --- | --- |
| R1 / G1 / B1 | 25 / 26 / 27 | 4 / 5 / 6 |
| R2 / G2 / B2 | 14 / 12 / 13 | 7 / 15 / 16 |
| A / B / C / D | 23 / 19 / 5 / 17 | 18 / 8 / 3 / 42 |
| CLK / LAT / OE | 16 / 4 / 15 | 40 / 41 / 2 |
| E | Unused (`-1`) | Unused (`-1`) |

These are example development-board assignments. Some pins are strapping pins, and specific module variants reserve pins for flash or PSRAM. Confirm that every selected pin is usable on your board and that the panel uses the expected scan pattern. This code does not establish compatibility with an unidentified matrix controller.

1. Supply the matrix through its designated 5 V power connector using a supply sized for the panel. Connect grounds. Do not run panel power through an ESP32 GPIO.
2. Check that HUB75 is connected to the panel's input connector and that the ribbon orientation matches the board.
3. Copy `secrets.example.h` to `secrets.h`; fill in your Wi-Fi network, service address, and device token.
4. Compile the correct PlatformIO environment. Connect USB, upload, and view the serial monitor at 115200 baud.
5. Start the Python service in sample mode first. Save a recognizable layout, then look for the same revision in both the serial log and editor's device status.
6. Check color order, pixel position, flicker, brightness, and Wi-Fi stability on the actual panel. Then enable OpenSky.

For local operation, `SERVICE_URL` resembles `http://192.168.1.100:8000`, without a trailing slash. Use the service computer's LAN address, and allow its port through the computer's firewall. Keep local HTTP on a trusted network. For an internet-hosted service, use HTTPS and paste the issuing CA certificate into `ROOT_CA`; NTP supplies the clock required for certificate validation. The firmware does not disable certificate checks.

If the display is blank, check panel power, input direction, GPIO mapping, and scan type. If HTTP reports 401, the firmware needs `DEVICE_TOKEN`, not `ADMIN_TOKEN`. If it reports unexpected length or a checksum error, inspect the server/proxy response; the firmware intentionally rejects a partial frame. The default driver setup targets common 1/16-scan panels; unusual scan patterns or driver chips may need board-specific changes.

Driver reference: [ESP32 HUB75 MatrixPanel DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA). Use your controller manufacturer's schematic as the final authority for its wiring.
