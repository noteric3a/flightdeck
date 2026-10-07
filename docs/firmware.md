# Autonomous ESP32 firmware

## Supported build profiles

`esp32s3` is the default and preserves the uploaded working **Waveshare ESP32-S3 RGB Matrix N32R16** setup: 32 MiB Octal flash, 16 MiB Octal PSRAM, USB CDC at boot, FM6126A panel driver, `clkphase=false`, E=9. The former generic S3 config header's CLK/LAT reversal has been corrected to match the working library defaults.

`esp32dev` compiles for a generic classic ESP32 with the explicit pin map below. Neither compilation nor the previous demo is proof of operation on another controller, scan ratio or panel driver.

| HUB75 signal | Waveshare S3 profile | Generic ESP32 profile |
| --- | --- | --- |
| R1 / G1 / B1 | 4 / 6 / 5 | 25 / 26 / 27 |
| R2 / G2 / B2 | 7 / 16 / 15 | 14 / 12 / 13 |
| A / B / C / D | 18 / 8 / 3 / 42 | 23 / 19 / 5 / 17 |
| E | 9 | Unused |
| LAT / OE / CLK | 40 / 2 / 41 | 4 / 15 / 16 |

One 64 × 32 matrix, 1/16 scan, is supported by the renderer. The Waveshare profile passes the pins in `include/config.h` explicitly; green and blue are exchanged relative to the library's S3 defaults because the panel otherwise shows blue as green. The library receives E=9 on Waveshare to preserve the tested configuration even though that line is unused on a typical 32-row panel. Use a common ground and an appropriate external 5 V matrix supply. Brightness defaults to 25/255. DMA uses double buffering and six-bit driver color depth to leave room for TLS; payload colors remain RGB565.

## Build and upload

Use the PlatformIO terminal in VS Code, or create a dedicated Python environment from the repository root. Keep PlatformIO separate from `requirements-dev.txt`: the legacy service and PlatformIO require incompatible Uvicorn/Starlette versions.

```bash
python3 -m venv firmware/.venv
source firmware/.venv/bin/activate
python -m pip install -r requirements-firmware.txt
python -m pip check
pio run -d firmware -e esp32s3
pio run -d firmware -e esp32s3 -t upload
```

On Windows, activate with `firmware\.venv\Scripts\activate` instead. Run `deactivate` before switching to the service test environment. The firmware environment is excluded from Git and source archives.

Choose `esp32dev` only for a classic ESP32 with verified matching wiring. Libraries and platform versions are pinned in `platformio.ini`. If changing from a previous local build produces a damaged archive/index error, run `pio run -d firmware -t clean`, then rebuild.

No compile-time Wi-Fi/API secrets are required. Provision with Studio over USB at 115200 baud. Close Serial Monitor before connecting Studio. The S3 profile uses native USB CDC; the classic ESP32 uses its USB-to-UART bridge. A data-capable USB cable is required.

The `huge_app.csv` map provides a 3 MiB app and a LittleFS partition within the first 4 MiB. No OTA implementation is included. The first successful filesystem mount is remembered in NVS. Blank/new flash can be formatted once; a later mount failure is reported without automatically erasing a saved layout. Two checksummed layout files allow fallback after an interrupted update.

## Runtime

The Arduino loop handles serial commands and matrix rendering. A separate core-0 task owns Wi-Fi, NTP, HTTP/TLS and AeroAPI. Queue snapshots carry immutable settings and flight results between tasks; generations prevent applying a response to an obsolete configuration. USB input lines, decoded transfers, provider response bytes, JSON allocation, flight count and logo pixels are bounded.

`standalone.h` contains the binary layout validator, model, filters and renderer. `aeroapi.h` normalizes provider data. `main.cpp` handles hardware, task coordination, provisioning, storage and USB. `default_layout.h` and `font_data.h` are generated from the Studio assets with `node scripts/generate_device_assets.mjs`.

The embedded certificate bundle comes from certifi's Mozilla public trust anchors, not a workstation/proxy store. It is used with hostname validation and a synchronized clock; there is no `setInsecure()` fallback. Regenerate it with `python scripts/generate_cert_bundle.py` after installing current `certifi` and `cryptography`, then rebuild/reflash when trust roots need updating. The provenance and license are in `firmware/certs/`.

Settings are stored in unencrypted NVS. API keys/passwords never appear in status responses. The request counter has its own namespace and survives settings changes/credential removal. Full flash erase resets all state.

## Standalone offline test

`firmware/examples/offline-demo/main.cpp` is based on the supplied working demo. Its logo bitmaps, RGB565 palettes and aircraft/plane colors now come from `firmware/include/offline_logo_data.h`, generated from the same Studio defaults as the autonomous firmware. Keep that header available when building the example. The sketch is outside `src/`, so it cannot define a second `setup()`/`loop()` in the normal build. The uploaded backup sketches remain in `firmware/backup/`. The current firmware has its own offline demo and normally does not require swapping source files to test the panel.
