# Validation record

Verified on 2026-09-09 with Python 3.12.14 and Node.js 24.19.0.

| Check | Result |
| --- | --- |
| Python tests | 53 passed, including service startup, device simulation, cached flight details, lookup limits, active-flight matching, and variable-size logo validation |
| JavaScript tests | 13 passed for movement, toggles, filtering, units, validation, all five presets, preserving drafts/imports, ETA, and progress-bar boundaries |
| Browser/Python rendering parity | Matching RGB565 bytes across units, missing values, custom logos, text scaling, stale/idle/offline states, routes, aircraft, ETA, and start/midpoint/end/unknown progress |
| Firmware frame decoder | Compiled with a native C++ compiler and exercised against Python-generated packets, truncation, and checksum corruption |
| ESP32 firmware | PlatformIO `esp32dev` build succeeded; 50,920 bytes static RAM and 934,177 bytes flash. Runtime DMA buffers are allocated separately. |
| ESP32-S3 profile | Included, but its build was not completed because the additional toolchain download could not finish in this environment |
| Editor assets | JavaScript syntax and local entrypoints checked |
| Airline logo images | Four native 28 × 28 PNGs checked against the exact configured pixel arrays; panel examples rendered from the editor's frame renderer |
| Python dependencies | Installed; dependency consistency check passed |

The screenshot-default update reran all 13 JavaScript tests and 40 affected Python tests. It verifies migration from the previous released default while preserving custom drafts, and verifies different departure/arrival airports returned by the API for different selected flights.

The Python suite exercises OpenSky and FlightAware clients using deterministic mock responses, including token renewal, flight selection, cache expiry, shared access/rate cooldowns, and request caps. It does not verify a real OpenSky or FlightAware account, and no paid API requests were made. Two upstream test-library deprecation warnings remain; they do not fail the tests.

The suite launches the production Python entry point and receives a valid frame using the supplied simulator. Docker is not installed in the authoring environment, so the Docker image itself has not been built or run here. The CI workflow includes its build. A cloud Python backend has not been provisioned; it requires a connected hosting account and accessible source repository.

Physical panel output, the user's exact controller pinout, Wi-Fi connectivity, and HTTPS certificate configuration are unverified. Browser interaction/visual tests were not run; the JavaScript tests exercise editor logic rather than browser automation. The GUI's sample flights are synthetic and labeled as such.

The successful ESP32 build used Espressif platform 6.12.0, Arduino-ESP32 2.0.17, HUB75 DMA 3.0.15, Adafruit GFX 1.12.6, and Adafruit BusIO 1.17.4. It used the example credentials solely to verify compilation. Configure real credentials and confirm the board pinout before uploading.
