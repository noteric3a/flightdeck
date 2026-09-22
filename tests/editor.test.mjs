import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import {
  moveElement,
  filterFlights,
  sampleFlights,
  fieldText,
  renderPixels,
  overlaps,
  rgb565,
  clone,
} from "../dist/core.mjs";
import { validateConfig } from "../dist/validate.mjs";
import { layoutPreset, upgradeJourneyDraft, upgradeLegacyDraft } from "../dist/presets.mjs";
import { journeyState, remainingText } from "../dist/journey.mjs";
const defaults = JSON.parse(
  fs.readFileSync(new URL("../dist/defaults.json", import.meta.url)),
);
const font = JSON.parse(
  fs.readFileSync(new URL("../dist/font.json", import.meta.url)),
);

test("dragging the logo snaps to pixels and clamps all edges", () => {
  const logo = defaults.layout.elements[0];
  assert.equal(moveElement(logo, 33.3, 2.2).x, 33);
  assert.deepEqual(
    [moveElement(logo, 999, 999).x, moveElement(logo, 999, 999).y],
    [44, 12],
  );
  assert.deepEqual(
    [moveElement(logo, -999, -999).x, moveElement(logo, -999, -999).y],
    [0, 0],
  );
  assert.equal(logo.x, 2);
});
test("hide time remaining removes its pixels; moving logo changes its location", () => {
  const c = clone(defaults),
    f = sampleFlights(c.filters)[0];
  const before = renderPixels(c, f, font);
  c.layout.elements.find((e) => e.field === "eta").visible = false;
  const after = renderPixels(c, f, font);
  assert.notDeepEqual(before, after);
  c.layout.elements[0].x = 34;
  assert.notDeepEqual(renderPixels(c, f, font), after);
  assert.equal(rgb565(after).length, 4096);
});
test("filtering and sorting use location, altitude, operator, and ground state", () => {
  const f = clone(defaults.filters),
    all = sampleFlights(f, 1000);
  assert.equal(filterFlights(all, f, 1000).length, 5);
  assert.equal(
    filterFlights(all, { ...f, airlines: ["DAL"] }, 1000)[0].callsign,
    "DAL1082",
  );
  assert.equal(filterFlights(all, { ...f, radius_km: 1 }, 1000).length, 0);
  assert.equal(
    filterFlights(all, { ...f, include_ground: true }, 1000).length,
    6,
  );
  assert.equal(
    filterFlights(all, { ...f, sort_by: "altitude" }, 1000)[0].altitude_ft,
    35000,
  );
});
test("missing values stay missing and units convert correctly", () => {
  const f = sampleFlights(defaults.filters)[0];
  assert.equal(fieldText("altitude", f, "aviation"), "35000FT");
  assert.equal(fieldText("altitude", f, "metric"), "10668M");
  assert.equal(
    fieldText("speed", { ...f, speed_knots: null }, "metric"),
    "--KMH",
  );
  assert.equal(
    fieldText("vertical_rate", { ...f, vertical_rate_fpm: -1.5 }, "aviation"),
    "-1FPM",
  );
});
test("configuration round-trips and rejects corrupt or oversized layouts", () => {
  assert.deepEqual(
    validateConfig(JSON.parse(JSON.stringify(defaults))),
    defaults,
  );
  for (const change of [
    (c) => (c.layout.elements[0].x = 60),
    (c) => (c.layout.width = 128),
    (c) => (c.filters.latitude = Infinity),
    (c) => (c.layout.elements[1].id = "logo"),
    (c) => (c.filters.min_altitude_ft = 70000),
    (c) => (c.logos.UAL = { width: 12, height: 12, pixels: [] }),
    (c) => (c.rotation_seconds = 0),
  ]) {
    const c = clone(defaults);
    change(c);
    assert.throws(() => validateConfig(c));
  }
});
test("only visible overlapping elements produce warnings", () => {
  assert.deepEqual(overlaps(defaults.layout.elements), []);
  const c = clone(defaults);
  c.layout.elements.find((e) => e.field === "speed").visible = true;
  assert.ok(overlaps(c.layout.elements).length > 0);
});

test("all presets stay on-panel without overlapping visible layers", () => {
  for (const name of ["journey", "large", "right", "classic", "minimal"]) {
    const c = clone(defaults);
    c.layout.elements = layoutPreset(defaults, name);
    validateConfig(c);
    assert.deepEqual(overlaps(c.layout.elements), []);
  }
  assert.equal(layoutPreset(defaults, "right")[0].x, 42);
  assert.deepEqual([defaults.layout.elements[0].width, defaults.layout.elements[0].height], [20, 20]);
});

test("old drafts gain pixel logos without replacing custom layouts or uploads", () => {
  const c = clone(defaults);
  c.layout.elements = layoutPreset(defaults, "classic").slice(0, 8);
  c.logos = {};
  const upgraded = upgradeLegacyDraft(c, defaults);
  assert.deepEqual(upgraded.layout.elements, defaults.layout.elements);
  assert.deepEqual(upgraded.logos, defaults.logos);
  c.layout.elements[0].x = 45;
  c.logos.UAL = {width: 12, height: 12, pixels: Array(144).fill("#ff0000")};
  const custom = upgradeLegacyDraft(c, defaults);
  assert.deepEqual(custom.layout.elements.slice(0, 8), c.layout.elements);
  assert.ok(custom.layout.elements.slice(8).every((e) => !e.visible));
  assert.deepEqual(custom.logos.UAL, c.logos.UAL);
  assert.deepEqual(validateConfig(custom), custom);
});

test("journey migration upgrades the untouched 28px layout and preserves custom drafts", () => {
  const c = clone(defaults);
  c.layout.elements = layoutPreset(defaults, "large").slice(0, 8);
  assert.deepEqual(upgradeJourneyDraft(c, defaults).layout.elements, defaults.layout.elements);
  assert.deepEqual(upgradeJourneyDraft(c, defaults, false).layout.elements.slice(0, 8), c.layout.elements);
  c.layout.elements[0].x = 30;
  c.layout.elements[1].id = "route";
  const migrated = upgradeJourneyDraft(c, defaults);
  assert.deepEqual(migrated.layout.elements.slice(0, 8), c.layout.elements);
  assert.equal(migrated.layout.elements.find((e) => e.field === "route").id, "route_a");
  assert.deepEqual(validateConfig(migrated), migrated);
  assert.deepEqual(upgradeJourneyDraft(migrated, defaults), migrated);
});

test("the previous released default upgrades without resetting custom layouts or imports", () => {
  const c = clone(defaults);
  c.layout = JSON.parse(fs.readFileSync(new URL("./fixtures/previous-default-layout.json", import.meta.url)));
  c.filters.radius_km = 80;
  c.logos.UAL = {width: 12, height: 12, pixels: Array(144).fill("#ff0000")};
  const upgraded = upgradeJourneyDraft(c, defaults);
  assert.deepEqual(upgraded.layout.elements, defaults.layout.elements);
  assert.deepEqual(upgraded.filters, c.filters);
  assert.deepEqual(upgraded.logos, c.logos);
  assert.deepEqual(upgradeJourneyDraft(c, defaults, false), c);
  c.layout.elements.find((e) => e.field === "route").y = 9;
  assert.deepEqual(upgradeJourneyDraft(c, defaults), c);
});

test("route, aircraft and ETA handle missing data and cross-day timestamps", () => {
  const f = {departure_airport: "ORD", arrival_airport: "HND", aircraft_type: "B787-9", departure_time: 100000, arrival_time: 143200};
  assert.equal(fieldText("route", f, "aviation"), "ORD->HND");
  assert.equal(fieldText("aircraft", f, "aviation"), "B787-9");
  assert.equal(fieldText("eta", f, "aviation", 136000), "2H00M");
  assert.equal(remainingText(f, 143199), "1M");
  assert.equal(remainingText(f, 150000), "0M");
  assert.equal(fieldText("route", {}, "aviation"), "---->---");
  assert.equal(fieldText("aircraft", {}, "aviation"), "--");
  assert.equal(remainingText({}, 136000), "--");
  assert.deepEqual(journeyState({}, 136000), {minutes: null, progress: null});
  assert.equal(journeyState(f, 1).progress, 0);
  assert.equal(journeyState(f, 121600).progress, 0.5);
  assert.equal(journeyState(f, 150000).progress, 1);
  assert.equal(journeyState({progress_percent: 25}, 136000).progress, 0.25);
});

test("progress bar uses exact green and white with a bounded plane at start, midpoint and end", () => {
  const c = clone(defaults), f = {progress_percent: 50};
  const e = c.layout.elements.find((e) => e.field === "progress");
  c.layout.elements = [e];
  const middle = renderPixels(c, f, font, "fresh", 100000);
  const row = (e.y + 3) * 64;
  assert.equal(middle[row + e.x], "#00ff00");
  assert.equal(middle[row + e.x + e.width - 1], "#ffffff");
  assert.equal(middle[row + e.x + 17], e.color);
  for (const percent of [0, 100]) {
    const pixels = renderPixels(c, {progress_percent: percent}, font);
    assert.ok(pixels.includes(e.color));
    assert.ok(!pixels.includes(percent === 0 ? "#00ff00" : "#ffffff"));
    assert.equal(pixels.length, 64 * 32);
  }
  const unknown = renderPixels(c, {}, font);
  assert.ok(!unknown.includes("#00ff00") && !unknown.includes("#ffffff") && !unknown.includes(e.color));
  e.height = 6;
  assert.throws(() => validateConfig(c));
});

test("logo imports preserve legacy bitmaps and reject mismatched dimensions or excess pixels", () => {
  const c = clone(defaults);
  c.logos.UAL = {width: 12, height: 12, pixels: Array(144).fill(null)};
  validateConfig(c);
  c.logos.UAL.width = 28;
  assert.throws(() => validateConfig(c));
  c.logos = Object.fromEntries(Array.from({length: 9}, (_, i) =>
    [`AA${String.fromCharCode(65 + i)}`, {width: 32, height: 32, pixels: Array(1024).fill(null)}]));
  assert.throws(() => validateConfig(c));
});
