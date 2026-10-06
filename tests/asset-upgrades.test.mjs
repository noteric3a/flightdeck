import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { clone } from '../dist/core.mjs';
import { encodeLayout, decodeLayout } from '../dist/device-codec.mjs';
import { upgradeDisplayAssets } from '../dist/asset-upgrades.mjs';
import { legacyLogos } from '../dist/legacy-logos.mjs';

const defaults = JSON.parse(fs.readFileSync(new URL('../dist/defaults.json', import.meta.url)));
function oldDraft() {
  const c = clone(defaults);
  for (const [code, old] of Object.entries(legacyLogos)) {
    const palette = old.palette.map(p => code === 'UAL' && p === '#0000ff' ? '#005daa' : p);
    c.logos[code] = {width: 28, height: 28, pixels: Array.from(old.rows.join(''), p => palette[Number(p)])};
  }
  for (const e of c.layout.elements)
    if (['aircraft', 'progress'].includes(e.field)) e.color = '#50c8ff';
  return c;
}

test('saved factory assets upgrade at RGB888 and board RGB565 precision, preserving layout and filters', () => {
  const source = oldDraft();
  source.filters.radius_km = 80;
  source.layout.elements.find(e => e.field === 'logo').x = 4;
  const original = clone(source);
  for (const old of [source, decodeLayout(encodeLayout(source))]) {
    const {config, changed} = upgradeDisplayAssets(old, defaults);
    assert.equal(changed, true);
    for (const code of ['UAL', 'DAL', 'SWA']) assert.deepEqual(config.logos[code], defaults.logos[code]);
    assert.deepEqual(config.filters, old.filters);
    assert.deepEqual(config.layout.elements.map(({color, ...e}) => e), old.layout.elements.map(({color, ...e}) => e));
    for (const e of config.layout.elements.filter(e => ['aircraft', 'progress'].includes(e.field))) assert.equal(e.color, '#80bfff');
    assert.equal(upgradeDisplayAssets(decodeLayout(encodeLayout(config)), defaults).changed, false);
  }
  assert.deepEqual(source, original);
});

test('custom uploads, absent logos, and custom layer colors survive factory corrections', () => {
  const source = oldDraft();
  source.logos.UAL.pixels[0] = '#123456';
  source.logos.DAL.pixels[0] = null;
  delete source.logos.SWA;
  for (const e of source.layout.elements)
    if (['aircraft', 'progress'].includes(e.field)) e.color = '#b260ff';
  assert.deepEqual(upgradeDisplayAssets(source, defaults), {config: source, changed: false});
  assert.equal(upgradeDisplayAssets(defaults, defaults).changed, false);
  source.layout.elements.find(e => e.field === 'aircraft').color = '#00ff00';
  assert.equal(upgradeDisplayAssets(source, defaults).config.layout.elements.find(e => e.field === 'aircraft').color, '#80bfff');
});
