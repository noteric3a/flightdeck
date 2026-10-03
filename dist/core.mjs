import { journeyState, remainingText, plane, progressColors } from "./journey.mjs";

export const labels = {
  logo: "Airline logo",
  callsign: "Flight number",
  airline: "Airline",
  altitude: "Altitude",
  speed: "Ground speed",
  distance: "Distance",
  heading: "Track",
  vertical_rate: "Vertical speed",
  route: "Departure → arrival",
  aircraft: "Aircraft type",
  eta: "Time remaining",
  progress: "Flight progress",
};
export const clone = (value) => JSON.parse(JSON.stringify(value));
export const clamp = (v, min, max) => Math.max(min, Math.min(max, v));
export const round = (v) => Math.floor(v + 0.5);
export function moveElement(element, x, y, width = 64, height = 32) {
  return {
    ...element,
    x: clamp(round(x), 0, width - element.width),
    y: clamp(round(y), 0, height - element.height),
  };
}
export function haversine(lat1, lon1, lat2, lon2) {
  const r = Math.PI / 180;
  const a =
    Math.sin(((lat2 - lat1) * r) / 2) ** 2 +
    Math.cos(lat1 * r) *
      Math.cos(lat2 * r) *
      Math.sin(((lon2 - lon1) * r) / 2) ** 2;
  return (
    6371 *
    Math.atan2(Math.sqrt(clamp(a, 0, 1)), Math.sqrt(1 - clamp(a, 0, 1))) *
    2
  );
}
export function filterFlights(flights, f, now = Date.now() / 1000) {
  return flights
    .map((x) => ({
      ...x,
      distance_km: haversine(f.latitude, f.longitude, x.latitude, x.longitude),
    }))
    .filter(
      (x) =>
        x.distance_km <= f.radius_km &&
        (f.include_ground || !x.on_ground) &&
        now - x.position_time <= f.max_position_age_s &&
        x.position_time <= now + 60 &&
        (!f.airlines.length || f.airlines.includes(x.airline_code)) &&
        (!f.callsign || x.callsign.includes(f.callsign.toUpperCase())) &&
        (x.altitude_ft != null
          ? x.altitude_ft >= f.min_altitude_ft &&
            x.altitude_ft <= f.max_altitude_ft
          : f.min_altitude_ft === 0 && f.max_altitude_ft === 60000) &&
        (x.speed_knots != null
          ? x.speed_knots >= f.min_speed_knots
          : f.min_speed_knots === 0),
    )
    .sort(
      (a, b) =>
        (f.sort_by === "altitude"
          ? (b.altitude_ft ?? -1) - (a.altitude_ft ?? -1)
          : a.distance_km - b.distance_km) || a.icao24.localeCompare(b.icao24),
    );
}
export function fieldText(field, f, units, now = Date.now() / 1000) {
  if (!f) return "";
  const metric = units === "metric";
  const val = (v, s) => (v == null ? "--" + s : round(v) + s);
  switch (field) {
    case "route":
      return `${f.departure_airport || "---"}->${f.arrival_airport || "---"}`;
    case "aircraft":
      return f.aircraft_type || "--";
    case "eta":
      return remainingText(f, now);
    case "logo":
      return f.airline_code || "---";
    case "callsign":
      return f.callsign || f.icao24;
    case "airline":
      return f.airline_name || f.airline_code || "UNKNOWN";
    case "altitude":
      return val(
        f.altitude_ft == null ? null : f.altitude_ft * (metric ? 0.3048 : 1),
        metric ? "M" : "FT",
      );
    case "speed":
      return val(
        f.speed_knots == null ? null : f.speed_knots * (metric ? 1.852 : 1),
        metric ? "KMH" : "KT",
      );
    case "distance":
      return val(
        f.distance_km == null ? null : f.distance_km / (metric ? 1 : 1.852),
        metric ? "KM" : "NM",
      );
    case "heading":
      return val(f.heading, "D");
    case "vertical_rate":
      return val(
        f.vertical_rate_fpm == null
          ? null
          : f.vertical_rate_fpm * (metric ? 0.00508 : 1),
        metric ? "M/S" : "FPM",
      );
    default:
      return "";
  }
}
export function renderPixels(config, flight, font, status = "fresh", now = Date.now() / 1000) {
  const l = config.layout,
    w = l.width,
    h = l.height,
    pixels = new Array(w * h).fill(l.background);
  const put = (x, y, color) => {
    if (x >= 0 && y >= 0 && x < w && y < h) pixels[y * w + x] = color;
  };
  const text = (s, e) => {
    let cx = e.x;
    for (const ch of s.toUpperCase()) {
      if (cx + 3 * e.scale > e.x + e.width) break;
      const g = font[ch] || font["?"];
      g.forEach((row, yy) =>
        [...row].forEach((v, xx) => {
          if (v === "1")
            for (let a = 0; a < e.scale; a++)
              for (let b = 0; b < e.scale; b++)
                if (yy * e.scale + a < e.height)
                  put(cx + xx * e.scale + b, e.y + yy * e.scale + a, e.color);
        }),
      );
      cx += 4 * e.scale;
    }
  };
  if (!flight) {
    text(
      ["stale", "unavailable"].includes(status) ? "DATA OFFLINE" : "NO FLIGHTS",
      { x: 2, y: 13, width: 60, height: 5, scale: 1, color: "#7a99ad" },
    );
    return pixels;
  }
  for (const e of l.elements) {
    if (!e.visible) continue;
    if (e.field === "progress") {
      const { progress } = journeyState(flight, now);
      const cy = e.y + Math.floor(e.height / 2);
      if (progress === null) {
        // Missing data is not a claim that the flight has made 0% progress.
        text("--", { ...e, y: cy - 2, height: 5, scale: 1, color: "#7a99ad" });
        continue;
      }
      const px = e.x + Math.floor(progress * (e.width - 7) + 0.5);
      const divider = px + 3;
      for (let x = e.x; x < e.x + e.width; x++)
        put(x, cy, x < divider ? progressColors.completed : progressColors.remaining);
      plane.forEach((row, y) => [...row].forEach((bit, x) =>
        put(px + x, cy - 3 + y, bit === "1" ? e.color : l.background)));
      continue;
    }
    const logo = e.field === "logo" && config.logos[flight.airline_code];
    if (logo) {
      for (let y = 0; y < e.height; y++)
        for (let x = 0; x < e.width; x++) {
          const c =
            logo.pixels[
              Math.floor((y * logo.height) / e.height) * logo.width +
                Math.floor((x * logo.width) / e.width)
            ];
          if (c) put(e.x + x, e.y + y, c);
        }
    } else if (e.field === "logo") {
      text(fieldText("logo", flight, l.units), {
        ...e,
        y: e.y + Math.max(0, Math.floor((e.height - 5) / 2)),
        scale: 1,
      });
    } else text(fieldText(e.field, flight, l.units, now), e);
  }
  if (status === "stale") put(w - 1, 0, "#f4b860");
  return pixels;
}
export function rgb565(pixels) {
  const b = new Uint8Array(pixels.length * 2);
  pixels.forEach((c, i) => {
    const r = parseInt(c.slice(1, 3), 16),
      g = parseInt(c.slice(3, 5), 16),
      bl = parseInt(c.slice(5, 7), 16);
    const n = ((r >> 3) << 11) | ((g >> 2) << 5) | (bl >> 3);
    b[2 * i] = n & 255;
    b[2 * i + 1] = n >> 8;
  });
  return b;
}
export function overlaps(elements) {
  const out = [];
  const v = elements.filter((e) => e.visible);
  for (let i = 0; i < v.length; i++)
    for (let j = i + 1; j < v.length; j++) {
      const a = v[i],
        b = v[j];
      if (
        a.x < b.x + b.width &&
        a.x + a.width > b.x &&
        a.y < b.y + b.height &&
        a.y + a.height > b.y
      )
        out.push([a.id, b.id]);
    }
  return out;
}
export function sampleFlights(filters, now = Date.now() / 1000, epoch = now) {
  // Illustrative trips; these are not the real schedules for the callsigns.
  const journeys = [
    ["PHL", "ORD", "B787-9", 510, 225],
    ["ATL", "JFK", "E175", 75, 45],
    ["MDW", "DEN", "B737-800", 35, 100],
    ["DFW", "LAX", "A321", 130, 25],
    [null, null, "C172", null, null],
    ["ORD", "SFO", "B737-800", null, null],
  ];
  const specs = [
    ["a00001", "UAL247", "UAL", "UNITED", 0.08, 0.07, 35000, 452, 78, 0],
    ["a00002", "DAL1082", "DAL", "DELTA", -0.16, 0.13, 28000, 421, 142, -640],
    ["a00003", "SWA516", "SWA", "SOUTHWEST", 0.25, -0.22, 22000, 395, 312, 800],
    ["a00004", "AAL903", "AAL", "AMERICAN", -0.41, -0.28, 32000, 448, 265, 0],
    ["a00005", "N625EC", "", "PRIVATE", 0.02, 0.04, 2100, 108, 185, 200],
    ["a00006", "UAL700", "UAL", "UNITED", 0.01, -0.015, 600, 0, 0, 0],
  ];
  return specs.map(
    (
      [
        icao24,
        callsign,
        airline_code,
        airline_name,
        lat,
        lon,
        altitude_ft,
        speed_knots,
        heading,
        vertical_rate_fpm,
      ],
      i,
    ) => ({
      icao24,
      callsign,
      airline_code,
      airline_name,
      latitude: clamp(filters.latitude + lat, -90, 90),
      longitude: ((filters.longitude + lon + 540) % 360) - 180,
      altitude_ft,
      speed_knots,
      heading,
      vertical_rate_fpm,
      on_ground: i === 5,
      position_time: now,
      last_contact: now,
      origin_country: "United States",
      departure_airport: journeys[i][0],
      arrival_airport: journeys[i][1],
      aircraft_type: journeys[i][2],
      departure_time: journeys[i][3] == null ? null : epoch - journeys[i][3] * 60,
      arrival_time: journeys[i][4] == null ? null : epoch + journeys[i][4] * 60,
      progress_percent: null,
      details_source: "sample",
    }),
  );
}
