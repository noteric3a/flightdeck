# Validation — autonomous ESP32 + USB Studio

Validation performed on 2026-10-03, using the uploaded working project and preserving the existing upstream PHL → ORD sample-route update.

| Check | Result |
| --- | --- |
| Existing Python suite (`python -m pytest -q`) | 75 passed; one upstream Starlette test-client deprecation warning |
| JavaScript + native renderer (`node --test tests/*.test.mjs`) | 19 passed |
| C++ pixel comparison | Exact RGB565 equality with Studio across presets, all fields, scale/units, missing values, progress endpoints, transparent logos and clipping |
| Native memory checks | Renderer/layout tests compiled with address and undefined-behavior sanitizers; malformed/truncated layouts rejected |
| Native AeroAPI fixtures (`python scripts/test_device_provider.py`) | Passed: altitude units, missing data, timestamps, radius/age, exact flight-ID enrichment |
| Browser integration (`node tests/studio.browser.mjs`) | Passed in headless Chromium 134 / Playwright 1.51.1 with a simulated Web Serial device |
| Browser workflow | USB connect/reconnect, settings save, clearing secret inputs, no secret localStorage, live edited frame, layout save/readback, disconnect/resume |
| Browser request audit | No `/api/` backend requests |
| ESP32-S3 build | Successful with the pinned PlatformIO platform/libraries |
| Classic ESP32 build | Successful with the pinned PlatformIO platform/libraries |
| Generated assets | `node scripts/generate_device_assets.mjs --check` passed |
| Source whitespace | `git diff --check` passed |

Build environment: Linux, PlatformIO 6.1.18, espressif32 6.12.0, Arduino-ESP32 2.0.17; Node 24.19.0; Python 3.12; native g++ with sanitizers. The S3 build retains the upload's Waveshare N32R16 settings. Clean builds were used after this environment produced an incremental static-archive index error.

## Not verified here

No physical ESP32/HUB75 was attached, no Wi-Fi credentials or AeroAPI key were supplied, and no paid FlightAware requests were made. Actual flashing, board startup, DMA/panel behavior, sustained heap usage under Wi-Fi/TLS + USB load, USB drivers, NVS/LittleFS power interruption behavior, network reconnection, and live account/endpoint access still need a test on the user's hardware. Software mocks cannot establish these. No physical FPS, latency, power or wireless stability measurements are claimed.

## First hardware acceptance check

1. Flash `esp32s3`; verify the offline sample appears with the USB computer disconnected and external power present.
2. Connect Studio in Chrome/Edge and confirm `panel_ready`, storage and USB status.
3. Enable Live on matrix, move a layer, and confirm the physical pixels match.
4. Save to ESP32, unplug USB, and power-cycle. Confirm the saved layout returns.
5. Provision a 2.4 GHz Personal network and AeroAPI key; enable live mode with a low request cap. Confirm direct data, units and counters in Studio.
6. Remove network access and restore it; verify stale/expired data handling and continued USB editing. Close Studio; confirm autonomous operation.

The former server-based validation record is retained in [validation-service.md](validation-service.md).
