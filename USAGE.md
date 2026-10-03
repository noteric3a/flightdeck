# Using Flightdeck without a flight-data server

## 1. Flash the autonomous firmware

Install PlatformIO in VS Code (or `python3 -m pip install platformio==6.1.18`). Open the project. Your uploaded working test used a **Waveshare ESP32-S3 RGB Matrix N32R16**, so `esp32s3` is the default build profile, with the same Octal flash/PSRAM, FM6126A driver, clock phase and GPIO E setting.

From the repository root:

```bash
pio run -d firmware -e esp32s3 -t upload
```

If PlatformIO cannot choose the correct USB port, list ports with `pio device list`, then append `--upload-port /dev/cu.usbmodem...` on macOS (or your Windows COM port). Use the USB port that exposes the firmware's USB CDC interface on the S3. Some boards have a separate UART programming connector.

The `esp32dev` profile is for a generic classic ESP32, not the Waveshare S3. See [firmware.md](docs/firmware.md) for wiring. No `secrets.h`, `.env`, service URL or token is needed by the current firmware.

Firmware now reserves a 3 MiB application slot and LittleFS in the first 4 MiB of flash using `huge_app.csv`. It does not implement OTA. The first upgrade from the former partition map may reset stored data; subsequent normal uploads keep settings. An explicit full flash erase removes Wi-Fi, key, layout and request-budget state.

Keep the HUB75 panel powered by its appropriate external 5 V supply. A USB data connection does not replace matrix power. Brightness starts at 25/255, matching the working demo. Increase it only within your power supply and panel limits.

## 2. Open Studio on your desktop

```bash
python3 scripts/studio.py
```

Open **http://localhost:8765** in desktop Chrome or Edge. No Python packages are needed for this launcher. It serves only `dist/` on the loopback interface. It does not fetch flight data or hold credentials.

Alternative: serve `dist/` from any trusted HTTPS static host. Use a top-level browser tab. Web Serial requires a supported browser and a secure context (HTTPS or localhost); opening `index.html` as a local file is not the supported route.

Close PlatformIO Serial Monitor, Arduino Serial Monitor and other programs using the same port. Click **Connect ESP32**, select the board, and allow serial access. This first device selection is required by the browser. On later loads or USB plug-in events, Studio reconnects only if there is exactly one previously authorized port matching the saved USB vendor/product identity. If multiple boards match, choose the correct one explicitly.

Studio verifies the `flightdeck` product and protocol version before sending settings. An old offline-only sketch cannot answer that handshake: flash the new firmware first.

## 3. Provision Wi-Fi and FlightAware

In **ESP32 settings**:

1. Enter the **2.4 GHz Wi-Fi name** and password. WPA/WPA2 Personal or an open network is supported. Enterprise Wi-Fi, captive portals and 5-GHz-only networks are not implemented; for campus testing, a compatible hotspot is the straightforward option.
2. Leave board mode at **Offline demo** for a no-account hardware check.
3. For live data, enter your own **FlightAware AeroAPI key**, select **Live FlightAware**, review the poll interval and hourly request cap, and save.
4. Watch the footer for the Wi-Fi IP and the feed line for provider status. Time synchronization must succeed before certificate-validated HTTPS starts.

Blank password/key fields preserve saved values. Changing the network name requires its password or the explicit **Open network** checkbox. **Forget credentials** clears both Wi-Fi and API credentials and puts the board back into demo mode. This does not erase the saved layout or reset the hourly request counter.

Secrets are cleared from the form after a successful save or closing the dialog. Status replies contain availability flags only. Browser drafts and exported layouts contain layout/filter data, not credentials. The board stores credentials in ordinary, unencrypted NVS; physical access to flash is outside this firmware's protection.

## 4. Edit and display layouts live

- Toggle **Live on matrix** after connecting. Studio sends complete RGB565 preview frames over USB as you move layers, change colors, select flights or import logos. It keeps only the newest pending frame if you edit faster than USB can transfer.
- Live preview is temporary and does not write flash for every drag. Stop preview to resume the saved autonomous layout.
- Click **Save to ESP32** to persist layout, logos, units, brightness, flight filters and rotation interval. It stops preview and resumes device rendering. Network filters take effect on the next allowed poll, without bypassing the request cap/cooldown.
- **Load board layout** reads the saved configuration back. Undo restores the previous draft if needed. Readback has RGB565 panel color precision; **Export layout** from the original draft preserves its original 24-bit colors.
- Layout JSON import/export, undo/redo and browser draft saving remain available offline. A dirty in-session draft is preserved when connecting; otherwise the board's saved layout is loaded.

If Studio closes, USB is unplugged, or a background tab stops sending preview frames, the board resumes its saved layout after four seconds. Keep external power connected if you want it to continue running after unplugging USB.

Studio's sample/selected-flight preview and the board's autonomous rotation are distinct: Live on matrix mirrors your selected preview; autonomous mode rotates the board's eligible flights. The table refresh button reads cached board status; it does not trigger additional billable calls.

## 5. Live data behavior

The ESP32 requests `GET /flights/search?max_pages=1&query=...` from `https://aeroapi.flightaware.com/aeroapi` with the `x-apikey` header. The query bounds your location; exact radius, age, altitude, speed, callsign and airline filtering runs on the board. Flights are sorted by distance or altitude.

The search response supplies position, route and aircraft type. Altitude is converted from FlightAware's hundreds of feet into feet. It does not supply a numeric vertical rate, so that field stays `--`. Search is airborne-only; the ground-aircraft checkbox is unavailable in live mode and works in demo mode.

When ETA/progress layers are visible, at most one rotating flight per poll gets `GET /flights/{fa_flight_id}?max_pages=1`. Matching uses the unique flight ID to avoid confusing another flight with the same callsign. Estimated arrival and progress are cached for ten minutes. Flights without usable estimates show `--`; ETA coverage is deliberately bounded by the request cap, not guaranteed for every displayed flight. Progress is estimated elapsed flight time, not geographic distance along the route.

One response page and at most 15 filtered flights are kept. If FlightAware reports another page, Studio reports truncation. Dense airspace can omit nearby flights that fall on later pages. Radius boxes crossing the date line use a full-longitude search, so narrow results can be especially incomplete there. The firmware never follows pagination links automatically.

Defaults: 300 seconds between polls; at most 24 attempted HTTP calls per **UTC clock hour**. Search, details, HTTP errors and failed network attempts all count. The counter is committed before each request and survives resets. An hour boundary can permit another batch; this is not a rolling-hour or dollar limit. Per-call/result-set charges depend on your FlightAware plan. Use its account controls and usage page for billing oversight.

HTTP 429 honors `Retry-After` (seconds or HTTP date), at least five minutes and up to one day. Authentication/permission errors suspend further attempts until the key changes or the board restarts after account access is corrected. Other failures wait at least five minutes. Changing filters or clicking refresh does not bypass a pending cooldown. Positions expire at the saved maximum age (300 seconds by default). No valid live positions means `DATA OFFLINE` or `NO FLIGHTS`; firmware does not silently substitute demos in live mode.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| No USB capability | Desktop Chrome/Edge, localhost or HTTPS, top-level tab |
| No selectable port | USB **data** cable, proper connector, OS USB/UART driver, board powered |
| Port busy / access denied | Close Serial Monitor and other Studio tabs using the board |
| Handshake timeout | Flash current firmware; wait for boot; select its CDC/UART port, not a different device |
| Wi-Fi never connects | Correct SSID/password; 2.4 GHz Personal network; no captive portal |
| Waiting for NTP | Network permits DNS and NTP; HTTPS waits for a trustworthy clock |
| API access denied | Valid AeroAPI key and account access to flight search; a normal website login is not an API key |
| No matching flights | Correct latitude/longitude, radius and filters; one-page limit; position freshness |
| ETA missing | No provider estimate yet, not yet enriched, detail lookup failed or request cap reached |
| Board did not keep an edit | Save to ESP32, then check for a success message; preview alone is temporary |
| Data stops after editing | Leave board in Live mode and retain external power; read feed status for cap/cooldown |
| Layout or flash unavailable | Check status; normal firmware won't automatically format a previously mounted filesystem |

## Implementation and tests

See [USB protocol](docs/protocol.md), [hardware](docs/firmware.md), [validation](docs/validation.md) and [security](SECURITY.md). The old Python backend is retained for reference in [legacy-service.md](docs/legacy-service.md), but is not in the current Studio/firmware path.

Primary references: [FlightAware OpenAPI specification](https://www.flightaware.com/commercial/aeroapi/resources/aeroapi-openapi.yml), [FlightAware AeroAPI](https://www.flightaware.com/commercial/aeroapi/), [Chrome Web Serial](https://developer.chrome.com/docs/capabilities/serial).
