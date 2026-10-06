import { clone } from './core.mjs';
import { color565 } from './device-codec.mjs';
import { legacyLogos } from './legacy-logos.mjs';

// Compare every pixel at panel precision, including opacity. A board readback
// expands RGB565 to RGB888, so string comparison would miss the old assets.
function matchesLogo(logo, old, palette) {
  if (!logo || logo.width !== 28 || logo.height !== 28) return false;
  const pixels = old.rows.join('');
  return logo.pixels.length === pixels.length && logo.pixels.every((color, i) =>
    color !== null && color565(color) === color565(palette[Number(pixels[i])])
  );
}

export function upgradeDisplayAssets(draft, defaults) {
  const config = clone(draft);
  let changed = false;
  for (const [code, old] of Object.entries(legacyLogos)) {
    const palettes = [old.palette];
    if (code === 'UAL') palettes.push(old.palette.map(c => c === '#0000ff' ? '#005daa' : c));
    if (palettes.some(palette => matchesLogo(config.logos[code], old, palette))) {
      // Avoid marking already-correct United pixels as an unsaved edit.
      const current = config.logos[code], bundled = defaults.logos[code];
      if (!current.pixels.every((c, i) => color565(c) === color565(bundled.pixels[i]))) {
        config.logos[code] = clone(bundled);
        changed = true;
      }
    }
  }
  for (const e of config.layout.elements) {
    if (e.id !== e.field || !['aircraft', 'progress'].includes(e.field)) continue;
    if (['#0000ff', '#50c8ff', '#00ff00'].some(old => color565(e.color) === color565(old))) {
      e.color = defaults.layout.elements.find(layer => layer.field === e.field).color;
      changed = true;
    }
  }
  return { config, changed };
}
