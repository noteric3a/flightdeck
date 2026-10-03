> **Legacy server workflow:** retained for the older server-backed implementation. Current Studio and firmware use direct AeroAPI + USB; start with [the current guide](../USAGE.md).

# Flightdeck Studio

A Python flight service and a working visual editor for a 64 × 32 HUB75 flight display. Start in sample mode, arrange the screen, then connect OpenSky and an ESP32.

## Try the editor

The published editor works immediately with clearly labeled sample flights. Drag the airline logo, text, or progress bar, use the checkboxes to show or hide stats, and try **Logo on right · 20px** in the Layout menu. Selected elements can also move with the arrow keys; hold Shift for five-pixel steps.

The browser keeps a local draft. **Export layout** downloads the complete configuration, including imported logos. **Import a saved layout** restores it. The display preview uses the same bitmap font and rendering rules as the Python service.

Pixel-style United (UAL), Delta (DAL), American (AAL), and Southwest (SWA) logo images are bundled. The default **Default · 20px logo** layout uses a **20 × 20** logo beside the flight number, departure/arrival airports (such as `ORD->HND`), and aircraft type. Time remaining and the flight progress bar sit below. Each is a movable, independently toggled layer. Completed progress is green (`#00ff00`), remaining progress is white (`#ffffff`), and a right-facing pixel plane divides them. The plane color is adjustable.

The previous **Large logo · 28px** and **Classic** presets remain available. Altitude, speed, distance, airline name, heading, and vertical speed remain optional layers. Progress needs a box of at least 7 × 7 pixels to contain the plane. Long text triggers a clipping warning; resize or move the layer to make room.

The default matches the supplied screenshot: the logo stays at `(2, 2)`, the right-hand text starts at column 27 on rows 2, 10, and 17, and the progress bar starts at `(27, 23)`. Time remaining stays at `(2, 24)`. Text boxes allow room for longer API identifiers while keeping that alignment. Fresh editors and new service installations use this layout. Untouched earlier defaults upgrade in the editor; custom drafts remain intact. Choose **Default · 20px logo** at any time to apply it, then **Save to display** for a connected device.

Select **Airline logo** to upload a PNG, JPEG, or WebP for the preview airline, restore its bundled pixel logo, use initials, or download the image pack. Uploads fit a 28 × 28 source bitmap and scale to the layer size with nearest-neighbor sampling. Older 12 × 12 logos still import and render. Unknown airlines use initials. Draft migration preserves customized positions, filters, and uploaded images; untouched older factory layouts receive the new 20 × 20 arrangement. Custom drafts gain the four new layers hidden, ready to enable and place. Connecting an older service applies the same migration in the editor; **Save to display** is still required to commit changes. Importing a file always preserves its existing arrangement and adds missing journey layers hidden.

The downloadable pack includes native 28 × 28 PNGs and a complete layout JSON for **Import a saved layout**. Images are pixel-art adaptations generated for this project; prompts and asset preparation are documented in `docs/pixel-logos.md`. A configuration supports bitmaps up to 32 × 32, at most 50 airline codes, and 8,192 logo pixels in total.

## Run the complete service

Requires Python 3.12 or newer. On macOS or Linux, open a terminal in this directory:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python scripts/setup_env.py
python -m backend
```

On Windows, activate with `.venv\Scripts\activate` after creating the environment.

Open `http://localhost:8000`. Click **Connect service**, use `http://localhost:8000` for the service URL, and copy `ADMIN_TOKEN` from your local `.env` file. The token stays in memory in the editor and is cleared on reload. The service will load its saved layout when you connect. Export your browser draft first if you want to preserve a different draft.

Sample mode works without an external flight account. It exercises the full Python API, filters, cache, saved settings, and ESP32 frame endpoint. Only the upstream flight source is simulated.

### Docker

Requires Docker with Compose. Generate the environment file once, then run:

```bash
python3 scripts/setup_env.py
docker compose up --build -d
```

Skip the first command if `.env` already exists. The generator refuses to overwrite it. Open `http://localhost:8000` and connect as above. SQLite settings survive container restarts in the named volume. `docker compose down` stops the service; adding `-v` would delete its stored settings.

The application runs as UID 10001. The container starts by assigning its data directory to that user, then drops privileges before starting the service. One process and one instance are intentional: caching and device presence are process-local; settings are stored in SQLite.

## Enable live flights

Set `FLIGHT_PROVIDER=opensky` in `.env`. To authenticate, create an OpenSky API client and set both `OPENSKY_CLIENT_ID` and `OPENSKY_CLIENT_SECRET`. Restart the service. The client obtains and refreshes OAuth tokens automatically, retries temporary errors, and honors provider rate-limit cooldowns. Keep these credentials on the server.

Without credentials, the client attempts anonymous access with a 240-second default cache TTL. Authenticated access and sample mode default to 30 seconds. An explicit `CACHE_TTL_SECONDS` overrides either default. Quotas depend on the account tier and queried area; the cache cannot guarantee uninterrupted access. The UI reports outages and stale data rather than substituting sample flights.

OpenSky state vectors include position, altitude, ground speed, true track, and vertical rate. They do not supply the route, aircraft model, or arrival estimate needed by the new layers. Airline names are best-effort matches from callsign prefixes, which can differ from the marketed airline. See the [OpenSky REST documentation](https://openskynetwork.github.io/opensky-api/rest.html) for authentication, fields, and current limits.

### Live route, aircraft, and arrival estimates

Set `AEROAPI_KEY` to your FlightAware AeroAPI key on the Python server, alongside `FLIGHT_PROVIDER=opensky`, and restart. This optional integration requests the selected flight from [`GET /flights/{ident}`](https://www.flightaware.com/commercial/aeroapi/resources/aeroapi-openapi.yml). It chooses a single matching, departed, unlanded flight; ambiguous matches stay unknown. Airport labels prefer IATA codes and fall back to ICAO codes. Common aircraft codes such as `B789` and `E75L` display as `B787-9` and `E175`.

Both departure and arrival codes come from that flight's API response; `ORD->HND` is only an illustrative route in sample mode. The editor identifies the route source and tells you if the server needs a key or the route API is temporarily unavailable.

These are billable lookups under your FlightAware plan. The service only enriches the flight currently selected for preview or device display, and only when a journey layer is visible. It caches each callsign for `DETAILS_CACHE_TTL_SECONDS` (300 by default), requests at most one result page, and caps requests at `AEROAPI_MAX_REQUESTS_PER_HOUR` (120 by default per service process). Negative results are cached too. Rate-limit and authentication failures trigger a shared cooldown. Rotation can select up to 20 flights, each requiring its own lookup. Adjust the limit to your plan; reaching it leaves uncached details unavailable while position data continues.

Time remaining uses the estimated runway arrival in UTC, rounded up to minutes. Progress is elapsed time from actual takeoff divided by estimated total flight time, clamped to 0–100%; it is an estimate, not geographic distance traveled. The provider's progress percentage is a fallback when timestamps are incomplete. The editor updates its countdown every second; the ESP32 receives updates with its normal frame polls. At or past the estimate, time shows `0M`, which does not confirm landing. Missing values show dashes, and an unavailable ETA never becomes a made-up arrival time. Sample trips are illustrative and labeled; live failures never substitute them.

No key is required for the test GUI or service sample mode. Replace the Python backend with this source version before using the new layers with an existing service; older backends do not recognize them. A firmware change is unnecessary because the new fields are rendered into the existing RGB565 frame.

## Use the editor with your service

- Enter the display's latitude and longitude and choose a radius, altitude range, speed floor, airline codes, and callsign filter.
- **Apply filters** changes the preview. **Save to display** commits the layout and filters to the Python service.
- The ESP32 receives saved settings on its next successful poll, normally within five seconds. It rotates through up to the first 20 matching flights every 15 seconds, ordered by the saved sort setting. `rotation_seconds` is configurable in exported JSON.
- Clicking **Preview** on a flight only changes the editor. It does not pin the physical display to that flight.
- An amber pixel at the upper right indicates stale flight data. Empty results display `NO FLIGHTS`; an upstream outage without usable cached data displays `DATA OFFLINE`.
- Later layers draw above earlier layers. The editor flags overlap and truncated text. Hiding a stat leaves its position available for another layer.

The hosted editor needs a reachable **HTTPS** backend. Set `CORS_ORIGINS` to the editor's exact origin, without a trailing slash. For local HTTP use, open the editor from `http://localhost:8000` or the service's LAN address. An HTTPS page cannot freely connect to an HTTP server on your local network.

## Connect the ESP32

1. See [firmware setup](docs/firmware.md) and confirm your controller model and HUB75 pin mapping.
2. Copy `firmware/include/secrets.example.h` to `firmware/include/secrets.h`.
3. Fill in Wi-Fi credentials, the Python service URL, and `DEVICE_TOKEN` from `.env`. Use your computer's LAN IP for local hardware, not `localhost`.
4. Install the development requirements and compile/upload with PlatformIO:

```bash
pip install -r requirements-dev.txt
pio run -d firmware -e esp32dev
pio run -d firmware -e esp32dev -t upload
pio device monitor -d firmware
```

Use `-e esp32s3` for an ESP32-S3 development board after setting its correct pins. These generic profiles do not claim a pinout match to every ESP32 matrix controller. The editor reports the last layout revision acknowledged by the connected device.

To test the complete device API without hardware:

```bash
python scripts/simulate_device.py --frames 3
```

This checks each packet's size and checksum, acknowledges the prior layout revision, and writes `frame.ppm` so you can inspect the received pixels.

## Deploy the Python backend

The published sample editor and the Python backend are separate deployment targets. The static editor does not run Python. The full app can run on your laptop, a Raspberry Pi capable of running Docker, or a cloud host that supports the Dockerfile.

`render.yaml` supplies an optional Render Blueprint with a persistent disk, generated admin/device tokens, and a health check. A Render account connection and a source repository accessible to Render are required. The configured service and disk incur provider charges; review the price in Render before creating them. The blueprint starts in sample mode so you can verify connectivity before setting live credentials. It has not provisioned a cloud backend for you.

For Render: put the source in your own Git repository, create a Blueprint from it, set the environment fields, and open the resulting HTTPS service URL. The service hosts its own editor at `/`. If using the separately published editor, add that editor's origin to `CORS_ORIGINS`. Configure the ESP32 with the backend URL and its issuing CA certificate. Deployment reference: [Render Blueprints](https://render.com/docs/blueprint-spec) and [persistent disks](https://render.com/docs/disks).

## Tests

Node.js 22+ and a C++ compiler let you run every check, including browser/Python pixel parity and the firmware packet decoder.

```bash
pip install -r requirements-dev.txt
pytest -q
node --test tests/editor.test.mjs
pio run -d firmware -e esp32dev -e esp32s3
```

The suite covers API authentication, layout persistence, optimistic concurrency, input validation, filter boundaries, antimeridian/polar searches, OAuth renewal, 429 cooldowns, retry behavior, concurrent cache refreshes, bounded stale fallback, corrupt packet rejection, and JavaScript/Python RGB565 parity. It also launches the real service process and runs the device simulator against HTTP.

CI runs the software tests, both firmware builds, and a Docker build. OpenSky and FlightAware tests use controlled mock responses. Journey tests cover flight matching, missing data, countdowns, progress boundaries, cache reuse, lookup limits, draft migration, and matching browser/device pixels. Physical Wi-Fi, panel scan rate, wiring, color order, and real account access need verification on your hardware. See [validation record](docs/validation.md) for checks actually run during delivery.

## Project map

| Location | Purpose |
| --- | --- |
| `backend/provider.py` | Async Python OpenSky API client and OAuth |
| `backend/details.py` | Optional FlightAware route, aircraft, and ETA enrichment |
| `backend/journey.py`, `dist/journey.mjs` | Countdown, estimated progress, and pixel plane |
| `backend/cache.py` | Shared TTL cache, bounded memory, stale fallback |
| `backend/flights.py` | Spatial query bounds, filtering, sample source |
| `backend/main.py` | FastAPI routes, authentication, device state |
| `backend/store.py` | SQLite persistence and revision checks |
| `backend/renderer.py` | Bitmap renderer and RGB565 packet encoding |
| `dist/` | The working editor, shared font, and default configuration |
| `firmware/` | ESP32 / ESP32-S3 firmware and frame decoder |
| `scripts/` | Local setup, device simulator, source packaging |
| `tests/` | Python, JavaScript, and cross-language checks |
| `Dockerfile`, `compose.yaml`, `render.yaml` | Local and cloud deployment definitions |
| `.github/workflows/ci.yml` | Automated validation pipeline |

The API and wire contract are documented in [protocol.md](docs/protocol.md). FastAPI also exposes OpenAPI documentation at `/docs` on the Python service.
