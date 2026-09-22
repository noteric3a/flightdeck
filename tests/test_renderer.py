import json
import shutil
import struct
import subprocess
import time
import zlib

import pytest

from backend.flights import filter_flights, sample_flights
from backend.models import ROOT, Logo, defaults
from backend.renderer import frame_packet, render_pixels, rgb565


@pytest.mark.parametrize(
    "units,status,custom_logo,scale,empty",
    [
        ("aviation", "fresh", False, 1, False),
        ("metric", "stale", False, 1, False),
        ("aviation", "fresh", True, 1, False),
        ("metric", "fresh", False, 2, False),
        ("aviation", "unavailable", False, 1, True),
        ("aviation", "stale", False, 1, True),
        ("aviation", "fresh", False, 1, True),
    ],
)
def test_browser_and_python_render_identical_pixels(
    units, status, custom_logo, scale, empty
):
    cfg = defaults()
    now = time.time()
    cfg.layout.units = units
    flight = filter_flights(sample_flights(cfg.filters), cfg.filters)[1]
    flight.vertical_rate_fpm = -500.5
    flight.speed_knots = None
    cfg.layout.elements[-1].visible = True
    cfg.layout.elements[1].scale = scale
    cfg.layout.elements[1].height = 5 * scale
    if custom_logo:
        cfg.logos[flight.airline_code] = Logo(
            pixels=["#ff0000" if i % 3 else None for i in range(144)]
        )
        cfg.layout.elements[0].x = 34
    if empty:
        flight = None
    script = """
import fs from 'node:fs';
import {renderPixels,rgb565} from './dist/core.mjs';
const v=JSON.parse(fs.readFileSync(0,'utf8'));
const font=JSON.parse(fs.readFileSync('./dist/font.json','utf8'));
process.stdout.write(Buffer.from(rgb565(renderPixels(v.config,v.flight,font,v.status,v.now))).toString('hex'));
"""
    out = subprocess.run(
        ["node", "--input-type=module", "-e", script],
        input=json.dumps(
            {
                "config": cfg.model_dump(),
                "flight": flight.model_dump() if flight else None,
                "status": status,
                "now": now,
            }
        ),
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=True,
    )
    assert bytes.fromhex(out.stdout) == rgb565(render_pixels(cfg, flight, status, now))


def test_cpp_decoder_accepts_python_frames_and_rejects_corruption(tmp_path):
    compiler = shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        pytest.skip("Install a C++ compiler to verify the firmware decoder")
    cfg = defaults()
    packet = frame_packet(cfg, sample_flights(cfg.filters)[0], 7, demo=True)
    packet_file = tmp_path / "frame.bin"
    packet_file.write_bytes(packet)
    cpp = tmp_path / "check.cpp"
    cpp.write_text("""
#include "frame_protocol.h"
#include <fstream>
#include <vector>
#include <cassert>
int main(int argc,char** argv) {
  std::ifstream f(argv[1],std::ios::binary);
  std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),{});
  assert(flightdeck::validFrame(b.data(),b.size()));
  assert(flightdeck::read32(b.data()+10)==7);
  assert(!flightdeck::validFrame(b.data(),b.size()-1));
  b.back() ^= 1;
  assert(!flightdeck::validFrame(b.data(),b.size()));
  b.back() ^= 1;
  b[0]='X';
  assert(!flightdeck::validFrame(b.data(),b.size()));
}
""")
    exe = tmp_path / "check"
    subprocess.run(
        [
            compiler,
            "-std=c++17",
            "-I",
            str(ROOT / "firmware/include"),
            str(cpp),
            "-o",
            str(exe),
        ],
        check=True,
        capture_output=True,
    )
    subprocess.run([str(exe), str(packet_file)], check=True)


def test_disabled_stat_changes_output_and_packet_crc():
    cfg = defaults()
    flight = sample_flights(cfg.filters)[0]
    before = frame_packet(cfg, flight, 1)
    next(e for e in cfg.layout.elements if e.field == "eta").visible = False
    after = frame_packet(cfg, flight, 2)
    assert before[22:] != after[22:]
    assert struct.unpack_from("<I", after, 18)[0] == zlib.crc32(after[22:])
