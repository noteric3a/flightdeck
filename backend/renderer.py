import json
import math
import struct
import time
import zlib

from .journey import COMPLETED, PLANE, REMAINING, journey_state, remaining_text
from .models import ROOT

FONT = json.loads((ROOT / "dist/font.json").read_text())


def rounded(value):
    # Match JavaScript's rounding, including negative halves.
    return math.floor(value + 0.5)


def field_text(field, flight, units, now=None):
    if flight is None:
        return ""
    metric = units == "metric"

    def val(v, s):
        return ("--" if v is None else str(rounded(v))) + s

    def convert(v, k):
        return None if v is None else v * k

    return {
        "logo": lambda: flight.airline_code or "---",
        "callsign": lambda: flight.callsign or flight.icao24,
        "airline": lambda: flight.airline_name or flight.airline_code or "UNKNOWN",
        "route": lambda: (
            f"{flight.departure_airport or '---'}->{flight.arrival_airport or '---'}"
        ),
        "aircraft": lambda: flight.aircraft_type or "--",
        "eta": lambda: remaining_text(flight, now),
        "progress": lambda: "",
        "altitude": lambda: val(
            convert(flight.altitude_ft, 0.3048 if metric else 1),
            "M" if metric else "FT",
        ),
        "speed": lambda: val(
            convert(flight.speed_knots, 1.852 if metric else 1),
            "KMH" if metric else "KT",
        ),
        "distance": lambda: val(
            convert(flight.distance_km, 1 if metric else 1 / 1.852),
            "KM" if metric else "NM",
        ),
        "heading": lambda: val(flight.heading, "D"),
        "vertical_rate": lambda: val(
            convert(flight.vertical_rate_fpm, 0.00508 if metric else 1),
            "M/S" if metric else "FPM",
        ),
    }[field]()


def render_pixels(config, flight, status="fresh", now=None):
    now = time.time() if now is None else now
    l = config.layout
    pixels = [l.background] * (l.width * l.height)

    def put(x, y, color):
        if 0 <= x < l.width and 0 <= y < l.height:
            pixels[y * l.width + x] = color

    def text(s, x, y, width, height, scale, color):
        cx = x
        for ch in s.upper():
            if cx + 3 * scale > x + width:
                break
            for yy, row in enumerate(FONT.get(ch, FONT["?"])):
                for xx, bit in enumerate(row):
                    if bit == "1":
                        for a in range(scale):
                            for b in range(scale):
                                if yy * scale + a < height:
                                    put(cx + xx * scale + b, y + yy * scale + a, color)
            cx += 4 * scale

    if flight is None:
        text(
            "DATA OFFLINE" if status in ("stale", "unavailable") else "NO FLIGHTS",
            2,
            13,
            60,
            5,
            1,
            "#7a99ad",
        )
        return pixels
    for e in l.elements:
        if not e.visible:
            continue
        if e.field == "progress":
            _, progress = journey_state(flight, now)
            cy = e.y + e.height // 2
            if progress is None:
                text("--", e.x, cy - 2, e.width, 5, 1, "#7a99ad")
                continue
            px = e.x + rounded(progress * (e.width - 7))
            divider = px + 3
            for x in range(e.x, e.x + e.width):
                put(x, cy, COMPLETED if x < divider else REMAINING)
            for yy, row in enumerate(PLANE):
                for xx, bit in enumerate(row):
                    put(px + xx, cy - 3 + yy, e.color if bit == "1" else l.background)
            continue
        logo = config.logos.get(flight.airline_code) if e.field == "logo" else None
        if logo:
            for y in range(e.height):
                for x in range(e.width):
                    color = logo.pixels[
                        (y * logo.height // e.height) * logo.width
                        + x * logo.width // e.width
                    ]
                    if color:
                        put(e.x + x, e.y + y, color)
        elif e.field == "logo":
            text(
                field_text("logo", flight, l.units),
                e.x,
                e.y + max(0, (e.height - 5) // 2),
                e.width,
                e.height,
                1,
                e.color,
            )
        else:
            text(
                field_text(e.field, flight, l.units, now),
                e.x,
                e.y,
                e.width,
                e.height,
                e.scale,
                e.color,
            )
    if status == "stale":
        put(l.width - 1, 0, "#f4b860")
    return pixels


def rgb565(pixels):
    result = bytearray()
    for color in pixels:
        r, g, b = (int(color[i : i + 2], 16) for i in (1, 3, 5))
        result.extend(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
    return bytes(result)


def frame_packet(config, flight, revision, status="fresh", demo=False):
    payload = rgb565(render_pixels(config, flight, status))
    flags = (
        (1 if status == "stale" else 0)
        | (2 if flight is None else 0)
        | (4 if status == "unavailable" else 0)
        | (8 if demo else 0)
    )
    # FLT1, width, height, brightness, flags, revision, byte length, CRC32 of payload.
    header = struct.pack(
        "<4sHHBBIII",
        b"FLT1",
        64,
        32,
        config.layout.brightness,
        flags,
        revision,
        len(payload),
        zlib.crc32(payload),
    )
    return header + payload
