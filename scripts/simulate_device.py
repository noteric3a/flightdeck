"""Poll and validate the real device protocol without an ESP32."""

import argparse
import os
import struct
import time
import zlib
from pathlib import Path

import httpx
from dotenv import load_dotenv

load_dotenv()
parser = argparse.ArgumentParser()
parser.add_argument("--url", default="http://127.0.0.1:8000")
parser.add_argument("--frames", type=int, default=3)
parser.add_argument("--output", type=Path, default=Path("frame.ppm"))
args = parser.parse_args()
token = os.environ.get("DEVICE_TOKEN", "")
if not token:
    raise SystemExit("Set DEVICE_TOKEN or run from a directory containing .env.")
ack = 0
with httpx.Client(timeout=30) as client:
    for i in range(args.frames):
        response = client.get(
            args.url.rstrip("/") + "/api/device/frame",
            headers={
                "Authorization": "Bearer " + token,
                "X-Device-ID": "python-simulator",
                "X-Frame-Ack": str(ack),
            },
        )
        response.raise_for_status()
        data = response.content
        if len(data) != 4118:
            raise SystemExit("Invalid frame length")
        magic, w, h, brightness, flags, revision, length, crc = struct.unpack(
            "<4sHHBBIII", data[:22]
        )
        pixels = data[22:]
        if (
            magic != b"FLT1"
            or (w, h) != (64, 32)
            or length != 4096
            or zlib.crc32(pixels) != crc
        ):
            raise SystemExit("Frame validation failed")
        rgb = bytearray()
        for (p,) in struct.iter_unpack("<H", pixels):
            rgb.extend(
                (
                    ((p >> 11) & 31) * 255 // 31,
                    ((p >> 5) & 63) * 255 // 63,
                    (p & 31) * 255 // 31,
                )
            )
        args.output.write_bytes(b"P6\n64 32\n255\n" + rgb)
        print(
            f"Frame {i + 1}: layout {revision}, brightness {brightness}, flags {flags}, CRC valid"
        )
        ack = revision
        if i + 1 < args.frames:
            time.sleep(5)
