import { clone } from "./core.mjs";

const classicGeometry = {
  logo: [2, 2, 12, 12, true],
  callsign: [16, 2, 46, 5, true],
  airline: [16, 10, 46, 5, true],
  altitude: [2, 20, 30, 5, true],
  speed: [35, 20, 27, 5, true],
  distance: [2, 27, 30, 5, true],
  heading: [35, 27, 27, 5, true],
  vertical_rate: [2, 27, 36, 5, false],
};
const largeGeometry = {
  logo: [2, 2, 28, 28, true],
  callsign: [32, 2, 31, 5, true],
  airline: [32, 10, 31, 5, false],
  altitude: [32, 10, 31, 5, true],
  speed: [32, 18, 31, 5, true],
  distance: [32, 26, 31, 5, true],
  heading: [32, 26, 31, 5, false],
  vertical_rate: [32, 26, 31, 5, false],
};
const journeyFields = ["route", "aircraft", "eta", "progress"];
// Original 20px factory arrangement, before the user's screenshot refinement.
const previousJourneyGeometry = {
  logo: [2, 2, 20, 20, true],
  callsign: [24, 1, 39, 5, true],
  airline: [24, 8, 39, 5, false],
  altitude: [24, 8, 39, 5, false],
  speed: [24, 15, 39, 5, false],
  distance: [24, 15, 39, 5, false],
  heading: [24, 15, 39, 5, false],
  vertical_rate: [24, 15, 39, 5, false],
  route: [24, 8, 39, 5, true],
  aircraft: [24, 15, 39, 5, true],
  eta: [2, 24, 23, 5, true],
  progress: [28, 23, 34, 7, true],
};

export function layoutPreset(defaults, choice) {
  const elements = clone(defaults.layout.elements);
  for (const e of elements) {
    if (choice === "classic" || choice === "large") {
      const geometry = (choice === "classic" ? classicGeometry : largeGeometry)[e.field];
      if (geometry) [e.x, e.y, e.width, e.height, e.visible] = geometry;
      else e.visible = false;
    } else if (choice === "right") {
      if (e.field === "logo") e.x = 42;
      else if (!["eta", "progress"].includes(e.field)) e.x = 2;
    } else if (choice === "minimal") {
      e.visible = ["logo", "callsign", "altitude", "speed"].includes(e.field);
    }
  }
  return elements;
}

// Upgrade the previous editor's local draft once. User-authored layers and
// uploaded airline bitmaps always take precedence over the bundled pack.
export function upgradeLegacyDraft(draft, defaults) {
  const next = upgradeJourneyDraft(draft, defaults);
  let pixels = Object.values(next.logos).reduce((n, logo) => n + logo.pixels.length, 0);
  for (const [code, logo] of Object.entries(defaults.logos)) {
    if (!next.logos[code] && Object.keys(next.logos).length < 50 && pixels + logo.pixels.length <= 8192) {
      next.logos[code] = clone(logo);
      pixels += logo.pixels.length;
    }
  }
  return next;
}

// Only untouched factory layouts are replaced. Custom layouts retain every
// layer, logo and filter; newly available journey layers start hidden.
export function upgradeJourneyDraft(draft, defaults, replaceFactory = true) {
  const next = clone(draft);
  const previousJourney = clone(defaults.layout.elements);
  for (const e of previousJourney)
    [e.x, e.y, e.width, e.height, e.visible] = previousJourneyGeometry[e.field];
  const factories = [previousJourney, ...["classic", "large"].map((choice) =>
    layoutPreset(defaults, choice).filter((e) => !journeyFields.includes(e.field)))];
  // Recognize shipped cyan factory layouts after the hardware default moved to blue.
  factories.push(...factories.map(elements => elements.map(e => ({...e,
    color: e.color === "#0000ff" ? "#50c8ff" : e.color,
  }))));
  const matchesFactory = factories.some((old) => {
    return next.layout.elements.length === old.length &&
      next.layout.elements.every((e, i) => Object.keys(old[i]).every((key) => e[key] === old[i][key]));
  });
  if (replaceFactory && matchesFactory) {
    next.layout.elements = clone(defaults.layout.elements);
    return next;
  }
  for (const field of journeyFields) {
    if (next.layout.elements.some((e) => e.field === field) || next.layout.elements.length >= 16) continue;
    const e = clone(defaults.layout.elements.find((e) => e.field === field));
    e.visible = false;
    let suffix = 0;
    while (next.layout.elements.some((layer) => layer.id === e.id)) e.id = `${field}_${String.fromCharCode(97 + suffix++)}`;
    next.layout.elements.push(e);
  }
  return next;
}
