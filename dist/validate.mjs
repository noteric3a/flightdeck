import { labels } from "./core.mjs";
export function validateConfig(c) {
  const fail = (message) => {
    throw new Error(message);
  };
  const num = (v, a, b) =>
    typeof v === "number" && Number.isFinite(v) && v >= a && v <= b;
  const integer = (v, a, b) => num(v, a, b) && Number.isInteger(v);
  const color = (v) => typeof v === "string" && /^#[a-f0-9]{6}$/i.test(v);
  if (!c || typeof c !== "object" || c.schema_version !== 1)
    fail("Choose a Flightdeck version 1 layout.");
  const l = c.layout,
    f = c.filters;
  if (
    !l ||
    l.width !== 64 ||
    l.height !== 32 ||
    !integer(l.brightness, 1, 255) ||
    !["aviation", "metric"].includes(l.units) ||
    !color(l.background)
  )
    fail("Invalid display settings. This editor supports a 64 × 32 panel.");
  if (
    !Array.isArray(l.elements) ||
    !l.elements.length ||
    l.elements.length > 16
  )
    fail("A display must contain 1–16 layers.");
  const ids = new Set();
  for (const e of l.elements) {
    if (
      !e ||
      typeof e.id !== "string" ||
      !/^[a-z_]{1,24}$/.test(e.id) ||
      ids.has(e.id) ||
      !Object.hasOwn(labels, e.field) ||
      !integer(e.x, 0, 63) ||
      !integer(e.y, 0, 31) ||
      !integer(e.width, 1, 64) ||
      !integer(e.height, 1, 32) ||
      e.x + e.width > 64 ||
      e.y + e.height > 32 ||
      !integer(e.scale, 1, 3) ||
      typeof e.visible !== "boolean" ||
      (e.field === "progress" && (e.width < 7 || e.height < 7)) ||
      !color(e.color)
    )
      fail("A layer is invalid or extends beyond the panel.");
    ids.add(e.id);
  }
  if (
    !f ||
    !num(f.latitude, -90, 90) ||
    !num(f.longitude, -180, 180) ||
    !num(f.radius_km, 1, 250) ||
    !num(f.min_altitude_ft, 0, 60000) ||
    !num(f.max_altitude_ft, 0, 60000) ||
    f.min_altitude_ft > f.max_altitude_ft ||
    !num(f.min_speed_knots, 0, 2000) ||
    !integer(f.max_position_age_s, 30, 600) ||
    !["distance", "altitude"].includes(f.sort_by) ||
    typeof f.include_ground !== "boolean" ||
    typeof f.callsign !== "string" ||
    !/^[A-Z0-9 ]{0,8}$/.test(f.callsign) ||
    !Array.isArray(f.airlines) ||
    f.airlines.length > 50 ||
    f.airlines.some((a) => typeof a !== "string" || !/^[A-Z]{3}$/.test(a))
  )
    fail("Check the flight filters and altitude range.");
  if (
    !integer(c.rotation_seconds, 5, 300) ||
    !c.logos ||
    typeof c.logos !== "object" ||
    Array.isArray(c.logos) ||
    Object.keys(c.logos).length > 50
  )
    fail("Invalid rotation or logo settings.");
  let totalPixels = 0;
  for (const [code, logo] of Object.entries(c.logos)) {
    if (
      !/^[A-Z]{3}$/.test(code) ||
      !logo ||
      !integer(logo.width, 1, 32) ||
      !integer(logo.height, 1, 32) ||
      !Array.isArray(logo.pixels) ||
      logo.pixels.length !== logo.width * logo.height ||
      logo.pixels.some((p) => p !== null && !color(p))
    )
      fail("Logos need a three-letter airline code and a matching bitmap up to 32 × 32 pixels.");
    totalPixels += logo.pixels.length;
  }
  if (totalPixels > 8192) fail("Your airline logos exceed the 8,192-pixel limit. Remove a logo before adding another.");
  return c;
}
