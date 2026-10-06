"""Export the exact Studio pixel data as PNGs, layouts and a reproducible ZIP.

Uses Python's standard library plus Node (the project's existing renderer).
"""
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def png(width, height, pixels):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    rows = bytearray()
    for y in range(height):
        rows.append(0)  # PNG's unfiltered, lossless RGB scanline
        for color in pixels[y * width:(y + 1) * width]:
            rows.extend(bytes.fromhex((color or '#000000')[1:]))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(rows), 9)) + chunk(b'IEND', b'')


def main():
    config = json.loads((ROOT / 'dist/defaults.json').read_text())
    rendered = json.loads(subprocess.check_output(['node', '--input-type=module', '-e', '''
import fs from 'node:fs';
import { clone, renderPixels, sampleFlights } from './dist/core.mjs';
import { layoutPreset } from './dist/presets.mjs';
const c = JSON.parse(fs.readFileSync('dist/defaults.json'));
const font = JSON.parse(fs.readFileSync('dist/font.json'));
const large = clone(c); large.layout.elements = layoutPreset(c, 'large');
const now = 1700021600;
const frames = sampleFlights(c.filters, now, now).slice(0,4).map(f => renderPixels(c, f, font, 'fresh', now));
process.stdout.write(JSON.stringify({large, frames}));
'''], cwd=ROOT, text=True))
    outputs = {}
    for code, logo in config['logos'].items():
        outputs[f'dist/logos/{code}.png'] = png(logo['width'], logo['height'], logo['pixels'])
    outputs['dist/logos/flight-details-layout.json'] = (json.dumps(config, indent=2) + '\n').encode()
    outputs['dist/logos/large-logo-layout.json'] = (json.dumps(rendered['large'], indent=2) + '\n').encode()
    # Four sample displays in a 2x2 sheet, at integer scale, without interpolation.
    width, height, scale, gap = 64 * 8 * 2 + 24 * 3, 32 * 8 * 2 + 24 * 3, 8, 24
    sheet = ['#161c24'] * (width * height)
    for index, frame in enumerate(rendered['frames']):
        ox, oy = gap + (index % 2) * (64 * scale + gap), gap + (index // 2) * (32 * scale + gap)
        for y in range(32 * scale):
            row = [frame[(y // scale) * 64 + x // scale] for x in range(64 * scale)]
            start = (oy + y) * width + ox
            sheet[start:start + len(row)] = row
    outputs['dist/logos/display-examples.png'] = png(width, height, sheet)
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as pack:
        members = {Path(name).name: data for name, data in outputs.items()}
        members['README.txt'] = (ROOT / 'dist/logos/README.txt').read_bytes()
        members['prompts.json'] = (ROOT / 'assets/logo-masters/prompts.json').read_bytes()
        for name, data in sorted(members.items()):
            info = zipfile.ZipInfo(name, (2026, 10, 6, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            pack.writestr(info, data)
    outputs['dist/airline-pixel-logos.zip'] = archive.getvalue()
    for name, data in outputs.items():
        path = ROOT / name
        if '--check' in sys.argv:
            if path.read_bytes() != data:
                raise SystemExit(f'Regenerate {name}: python scripts/generate_logo_pack.py')
        else:
            path.write_bytes(data)
    print(f'{len(outputs)} logo exports {"verified" if "--check" in sys.argv else "generated"}.')


if __name__ == '__main__':
    main()
