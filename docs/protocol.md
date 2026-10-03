# Flightdeck USB protocol v2

Transport is newline-delimited UTF-8 JSON over USB CDC or a USB-UART bridge at
115200 baud. Studio supports desktop Web Serial. The board is the only
FlightAware client; USB status/preview requests never perform a provider lookup.
The previous HTTP/FLT1 contract is retained in [legacy-protocol.md](legacy-protocol.md).

## Requests and replies

Each request has an unsigned `id` and a `cmd`. Responses echo the ID and include
`ok: true`, or `ok: false` with an `error` string. Diagnostic/non-JSON boot lines
are ignored by Studio. Input lines are limited to 1,199 bytes; malformed or
oversized lines are discarded until a newline. Partial lines expire after five
seconds. The browser bounds reply lines at 32 KiB.

```json
{"id":1,"cmd":"hello"}
{"id":1,"ok":true,"product":"flightdeck","protocol":2,"width":64,"height":32}
```

The actual hello includes the same safe settings/health fields as status, without
the flight array. Studio verifies product, protocol and geometry before sending
credentials. Only one transaction runs at a time, with a per-command timeout.
Pending operations reject on disconnect and cannot cross into a new USB session.

| Command | Request fields | Result / effect |
| --- | --- | --- |
| `hello` | None | Protocol/product, safe settings, health |
| `status` | None | Settings/health plus normalized `flights`, `clock`, `stale` |
| `configure` | Optional `ssid`, `password`, `api_key`, `mode`, `poll_seconds`, `max_per_hour` | Validate, persist one NVS settings blob, queue network reconfiguration |
| `forget_credentials` | None | Clear Wi-Fi/key, demo mode; retain layout and budget |
| `upload_begin` | `kind`: `layout` or `frame`, byte `length`, unsigned `crc` | Allocate bounded staging buffer; replace abandoned transaction |
| `upload_chunk` | Byte `offset`, base64 `data` | Append exactly at current offset; up to 384 decoded bytes |
| `upload_commit` | None | Check length/CRC and validate payload before applying |
| `upload_abort` | None | Release staging buffer |
| `resume` | None | End USB preview; render saved autonomous layout |
| `layout_info` | None | Saved binary `length` and CRC-32 |
| `layout_chunk` | Byte `offset` | Up to 384 saved bytes as base64 `data` |

`configure` preserves omitted fields. An explicit empty password means an open
network; an empty key clears it. Live mode requires a nonempty SSID/key. SSID is
at most 32 UTF-8 bytes; password is empty, 8–63 bytes, or a 64-character hex PSK;
key is at most 256 printable non-space ASCII bytes. Poll interval is 60–3,600
seconds; UTC-hour cap is 1–120 requests. No status/hello response includes either
secret. `ssid`, `has_password`, `has_api_key`, `mode`, `poll_seconds`,
`max_per_hour`, `wifi_connected`, `ip`, `message`, `requests_this_hour`,
`retry_in`, `panel_ready`, `storage_ready`, `preview`, `truncated`, `layout_crc`
and `free_heap` are available.

Flight rows reuse Studio's renderer field names. `icao24` is an opaque selection
ID in USB mode: it contains the FlightAware flight ID, not a claimed ICAO address.
Unavailable numeric data serializes as JSON null. Demo timestamps are translated
by Studio using the response `clock`, so an offline ESP32 without NTP still shows
valid sample ETA/progress. Live timestamps are provider UTC Unix seconds.

## Preview payload

A `frame` payload is exactly **4,097 bytes**:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 1 | Brightness, 1–255 |
| 1 | 4,096 | 2,048 row-major little-endian RGB565 pixels |

The transaction CRC-32 covers the entire payload including brightness. A valid
commit updates the matrix and renews a **4-second** preview lease. Preview data
is never written to flash. Stopping traffic returns to autonomous rendering;
`resume` does so immediately. Studio keeps one in-flight and one replaceable
pending frame to prevent drag events accumulating in a long queue.

## Saved layout payload: FDL2

A `layout` transaction holds at most **28,000 bytes**. All multi-byte integers and
IEEE-754 doubles below are little endian. A `string` is a one-byte length followed
by that many ASCII bytes. The editable JSON import/export remains schema v1;
`dist/device-codec.mjs` is its converter.

| Sequence | Encoding |
| --- | --- |
| Magic | Four ASCII bytes `FDL2` |
| Brightness / metric | u8 / boolean u8 |
| Background / rotation seconds | RGB565 u16 / u16 |
| Latitude, longitude, radius km, min altitude ft, max altitude ft, min speed knots | Six f64 values |
| Max position age / include ground / altitude sort | u16 / boolean u8 / boolean u8 |
| Callsign filter | String, at most 8 bytes |
| Airline filters | u8 count (0–50), then 3 ASCII bytes per code |
| Layers | u8 count (1–16), then records below |
| Logos | u8 count (0–50), then records below |

Layer: ID string (1–24 lowercase letters/underscores); u8 field index; u8 x, y,
width, height, scale, visible; u16 RGB565 color. Field order:
`logo, callsign, airline, altitude, speed, distance, heading, vertical_rate, route, aircraft, eta, progress`.
Layer IDs must be unique; all bounds and progress-icon dimensions are checked.

Logo: 3-letter uppercase code, u8 width, u8 height (each 1–32), then one RGB565 u16
and explicit opacity boolean u8 per pixel. Null/transparent pixels are distinct
from opaque black. Total logo pixels may not exceed 8,192; codes must be unique.
Trailing bytes, nonfinite coordinates, invalid flags or out-of-bounds layers are
rejected before replacing the existing layout.

A staging transaction expires after ten seconds of inactivity. Commit verifies
CRC-32 (standard IEEE polynomial, same as zlib), complete length, and the full
layout parser. Flash writes use a temporary file, a previous-copy fallback, and a
CRC header. Readback validates CRC again in Studio. A malformed/partial upload
never changes the active layout.

## Provider and task boundary

The network task receives settings/filter snapshots tagged with a generation.
Only matching-generation flight snapshots reach the display task. It owns all
Wi-Fi, NTP, TLS and HTTP work; serial/rendering continues on the Arduino task.
Provider JSON is filtered while streaming with a 128 KiB decoded-body ceiling,
24 KiB JSON document limit and timeout. Plain and chunked HTTP bodies are
supported; redirects and compressed bodies are not accepted.

Request budget records are persisted before transmission in a separate NVS
namespace as a UTC-hour/count pair, including search, detail and failed attempts.
The board refuses to reset the budget backwards after a clock rollback. HTTPS
uses the fixed AeroAPI origin, the bundled public roots and synchronized time.
