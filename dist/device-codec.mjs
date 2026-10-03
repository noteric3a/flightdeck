import { validateConfig } from './validate.mjs';

export const FIELDS = ['logo', 'callsign', 'airline', 'altitude', 'speed', 'distance', 'heading', 'vertical_rate', 'route', 'aircraft', 'eta', 'progress'];
export const MAX_LAYOUT_BYTES = 28000;
export function crc32(bytes) {
  let crc = 0xffffffff;
  for (const b of bytes) {
    crc ^= b;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (crc ^ 0xffffffff) >>> 0;
}
export const color565 = c => {
  const n = Number.parseInt(c.slice(1), 16);
  return ((n >>> 8) & 0xf800) | ((n >>> 5) & 0x07e0) | ((n >>> 3) & 0x001f);
};
const hex565 = n => '#' + [((n >>> 11) * 255 / 31), (((n >>> 5) & 63) * 255 / 63), ((n & 31) * 255 / 31)]
  .map(v => Math.round(v).toString(16).padStart(2, '0')).join('');

// FDL2: bounded binary layout, with RGB565 + explicit opacity for logos.
// JSON exports remain the lossless editable format; a board readback has panel color precision.
export function encodeLayout(config) {
  validateConfig(config);
  const bytes = [], u8 = v => bytes.push(v & 255), u16 = v => { u8(v); u8(v >>> 8); };
  const str = v => { u8(v.length); for (const ch of v) u8(ch.charCodeAt(0)); };
  const f64 = v => { const b = new Uint8Array(8); new DataView(b.buffer).setFloat64(0, v, true); bytes.push(...b); };
  bytes.push(70, 68, 76, 50);
  const l = config.layout, f = config.filters;
  u8(l.brightness); u8(l.units === 'metric'); u16(color565(l.background)); u16(config.rotation_seconds);
  for (const key of ['latitude', 'longitude', 'radius_km', 'min_altitude_ft', 'max_altitude_ft', 'min_speed_knots']) f64(f[key]);
  u16(f.max_position_age_s); u8(f.include_ground); u8(f.sort_by === 'altitude'); str(f.callsign);
  u8(f.airlines.length); for (const a of f.airlines) for (const ch of a) u8(ch.charCodeAt(0));
  u8(l.elements.length);
  for (const e of l.elements) {
    str(e.id); u8(FIELDS.indexOf(e.field));
    for (const k of ['x', 'y', 'width', 'height', 'scale', 'visible']) u8(e[k]);
    u16(color565(e.color));
  }
  u8(Object.keys(config.logos).length);
  for (const [code, logo] of Object.entries(config.logos)) {
    for (const ch of code) u8(ch.charCodeAt(0));
    u8(logo.width); u8(logo.height);
    for (const c of logo.pixels) { u16(c === null ? 0 : color565(c)); u8(c !== null); }
  }
  if (bytes.length > MAX_LAYOUT_BYTES) throw new Error('Layout is too large for this firmware.');
  return Uint8Array.from(bytes);
}

export function decodeLayout(bytes) {
  if (bytes.length > MAX_LAYOUT_BYTES) throw new Error('Oversized board layout.');
  let pos = 0;
  const take = n => { if (pos + n > bytes.length) throw new Error('Incomplete board layout.'); const b = bytes.subarray(pos, pos + n); pos += n; return b; };
  const u8 = () => take(1)[0], u16 = () => { const b = take(2); return b[0] | (b[1] << 8); };
  const text = n => String.fromCharCode(...take(n)), str = () => text(u8());
  const f64 = () => { const b = take(8); return new DataView(b.buffer, b.byteOffset, 8).getFloat64(0, true); };
  const bool = () => { const v = u8(); if (v > 1) throw new Error('Invalid board flag.'); return !!v; };
  if (text(4) !== 'FDL2') throw new Error('Unsupported board layout.');
  const layout = {width: 64, height: 32, brightness: u8(), units: bool() ? 'metric' : 'aviation', background: hex565(u16())};
  const rotation_seconds = u16(), filters = {};
  for (const k of ['latitude', 'longitude', 'radius_km', 'min_altitude_ft', 'max_altitude_ft', 'min_speed_knots']) filters[k] = f64();
  filters.max_position_age_s = u16(); filters.include_ground = bool(); filters.sort_by = bool() ? 'altitude' : 'distance'; filters.callsign = str();
  filters.airlines = Array.from({length: u8()}, () => text(3));
  layout.elements = Array.from({length: u8()}, () => {
    const e = {id: str(), field: FIELDS[u8()]};
    for (const k of ['x', 'y', 'width', 'height', 'scale']) e[k] = u8();
    e.visible = bool(); e.color = hex565(u16()); return e;
  });
  const logos = {};
  for (let i = u8(); i > 0; i--) {
    const code = text(3), width = u8(), height = u8();
    if (Object.hasOwn(logos, code)) throw new Error('Duplicate board logo.');
    const pixels = Array.from({length: width * height}, () => { const c = hex565(u16()); return bool() ? c : null; });
    logos[code] = {width, height, pixels};
  }
  if (pos !== bytes.length) throw new Error('Unexpected board layout data.');
  return validateConfig({schema_version: 1, layout, filters, rotation_seconds, logos});
}

export function toBase64(bytes) { return btoa(String.fromCharCode(...bytes)); }
export function fromBase64(text) { return Uint8Array.from(atob(text), c => c.charCodeAt(0)); }
