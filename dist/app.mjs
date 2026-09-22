import {
  labels,
  clone,
  clamp,
  moveElement,
  filterFlights,
  fieldText,
  renderPixels,
  overlaps,
  sampleFlights,
} from "./core.mjs";
import { validateConfig } from "./validate.mjs";
import { layoutPreset, upgradeJourneyDraft, upgradeLegacyDraft } from "./presets.mjs";
import { journeyState, remainingText } from "./journey.mjs";

const $ = (id) => document.getElementById(id);
const sampleEpoch = Date.now() / 1000;
const [defaults, font] = await Promise.all(
  ["/defaults.json", "/font.json"].map(async (path) => {
    const r = await fetch(path);
    if (!r.ok)
      throw new Error("Could not load display assets. Refresh this page.");
    return r.json();
  }),
);
let config = clone(defaults),
  selected = "logo",
  flights = [],
  flightId = "a00001",
  undo = [],
  redo = [],
  connected = null,
  revision = 0,
  feedState = "fresh",
  detailsState = "sample",
  requestSerial = 0,
  toastTimer,
  drag = null,
  dirty = false;
const safeStorage = {
  get(key) {
    try {
      return localStorage.getItem(key);
    } catch {
      return null;
    }
  },
  set(key, value) {
    try {
      localStorage.setItem(key, value);
      return true;
    } catch {
      return false;
    }
  },
};
try {
  const saved = safeStorage.get("flightdeck.draft.v4");
  if (saved) config = validateConfig(JSON.parse(saved));
  else {
    const previous = safeStorage.get("flightdeck.draft.v3") || safeStorage.get("flightdeck.draft.v2");
    const legacy = safeStorage.get("flightdeck.draft.v1");
    if (previous) config = validateConfig(upgradeJourneyDraft(validateConfig(JSON.parse(previous)), defaults));
    else if (legacy) config = validateConfig(upgradeLegacyDraft(validateConfig(JSON.parse(legacy)), defaults));
    safeStorage.set("flightdeck.draft.v4", JSON.stringify(config));
  }
} catch {
  notify("Saved draft could not be loaded. The default layout is ready.");
}
selected = config.layout.elements[0].id;
$("preset").value = JSON.stringify(config.layout.elements) === JSON.stringify(defaults.layout.elements) ? "journey" : "custom";
function notify(message) {
  $("toast").textContent = message;
  $("toast").hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => ($("toast").hidden = true), 5000);
}
function element() {
  return (
    config.layout.elements.find((e) => e.id === selected) ||
    config.layout.elements[0]
  );
}
function currentFlight() {
  return flights.find((f) => f.icao24 === flightId) || flights[0] || null;
}
function persist() {
  return safeStorage.set("flightdeck.draft.v4", JSON.stringify(config));
}
function checkpoint() {
  undo.push(clone(config));
  if (undo.length > 60) undo.shift();
  redo = [];
}
function changed() {
  dirty = true;
  persist();
  $("preset").value = "custom";
  render();
}
function mutate(fn) {
  checkpoint();
  fn();
  changed();
}
function selectLayer(id) {
  selected = id;
  renderLayers();
  renderInspector();
  renderHandles();
}
function node(tag, text, className) {
  const e = document.createElement(tag);
  if (text != null) e.textContent = text;
  if (className) e.className = className;
  return e;
}

function renderLayers() {
  const priorFocus = document.activeElement;
  const layerFocus = priorFocus?.dataset?.layerSelect;
  const toggleFocus = priorFocus?.dataset?.layerToggle;
  $("layers").replaceChildren();
  for (const e of config.layout.elements) {
    const row = node(
      "div",
      null,
      `layer-row${e.id === selected ? " selected" : ""}${e.visible ? "" : " off"}`,
    );
    const button = node("button", null, "layer-select");
    button.type = "button";
    button.dataset.layerSelect = e.id;
    button.setAttribute("aria-pressed", String(e.id === selected));
    button.append(
      node(
        "span",
        e.field === "logo" ? "▧" : e.field === "callsign" ? "Aa" : "≡",
        "layer-glyph",
      ),
      node("span", labels[e.field]),
    );
    button.onclick = () => selectLayer(e.id);
    const toggle = node("label", null, "layer-toggle");
    const input = document.createElement("input");
    input.type = "checkbox";
    input.dataset.layerToggle = e.id;
    input.checked = e.visible;
    input.setAttribute("aria-label", `Show ${labels[e.field]}`);
    input.onchange = () =>
      mutate(() => {
        e.visible = input.checked;
      });
    toggle.append(input);
    row.append(button, toggle);
    $("layers").append(row);
  }
  if (layerFocus)
    $("layers")
      .querySelector(`[data-layer-select="${layerFocus}"]`)
      ?.focus({ preventScroll: true });
  if (toggleFocus)
    $("layers")
      .querySelector(`[data-layer-toggle="${toggleFocus}"]`)
      ?.focus({ preventScroll: true });
  $("layer-count").textContent =
    `${config.layout.elements.filter((e) => e.visible).length} / ${config.layout.elements.length}`;
}
function renderInspector() {
  const e = element();
  $("inspector-name").textContent = labels[e.field];
  for (const key of ["x", "y", "width", "height", "scale", "color"])
    $("element-" + key).value = e[key];
  $("color-value").textContent = e.color.toUpperCase();
  $("element-x").max = 64 - e.width;
  $("element-y").max = 32 - e.height;
  $("element-width").max = 64 - e.x;
  $("element-height").max = 32 - e.y;
  $("element-width").min = $("element-height").min = e.field === "progress" ? 7 : 1;
  $("logo-options").hidden = e.field !== "logo";
  $("restore-logo").disabled = !defaults.logos[currentFlight()?.airline_code];
  $("text-scale-label").hidden = ["logo", "progress"].includes(e.field);
  $("element-color-label").textContent = e.field === "progress" ? "Plane color" : "Color";
  $("progress-options").hidden = e.field !== "progress";
  $("units").value = config.layout.units;
  $("brightness").value = config.layout.brightness;
  $("brightness-value").textContent =
    `${Math.round((config.layout.brightness / 255) * 100)}%`;
  $("cursor-position").textContent =
    `X ${String(e.x).padStart(2, "0")} · Y ${String(e.y).padStart(2, "0")}`;
  $("undo").disabled = undo.length === 0;
  $("redo").disabled = redo.length === 0;
  $("selection-hint").textContent = e.visible
    ? "Drag a layer or use the arrow keys."
    : "This layer is hidden. Enable it in Display layers.";
}
function paint() {
  const canvas = $("matrix"),
    rect = canvas.getBoundingClientRect(),
    dpr = window.devicePixelRatio || 1;
  const width = Math.max(64, Math.round(rect.width * dpr)),
    height = Math.max(32, Math.round(rect.height * dpr));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  const ctx = canvas.getContext("2d");
  ctx.fillStyle = "#010304";
  ctx.fillRect(0, 0, width, height);
  const pixels = renderPixels(config, currentFlight(), font, feedState);
  const sx = width / 64,
    sy = height / 32,
    gap = $("grid").checked ? Math.min(sx * 0.22, 2 * dpr) : 0;
  pixels.forEach((color, i) => {
    ctx.fillStyle =
      color === "#000000" && $("grid").checked ? "#10171c" : color;
    ctx.fillRect(
      (i % 64) * sx + gap / 2,
      Math.floor(i / 64) * sy + gap / 2,
      sx - gap,
      sy - gap,
    );
  });
  const f = currentFlight();
  $("preview-callsign").textContent = f?.callsign || "No flight";
  $("preview-airline").textContent = f?.airline_name || "";
  const { minutes, progress } = journeyState(f);
  const details = f ? [
    `${f.departure_airport || "---"} → ${f.arrival_airport || "---"}`,
    f.aircraft_type || "Aircraft unavailable",
    minutes == null ? "Arrival estimate unavailable" : `${remainingText(f).toLowerCase()} remaining (est.)`,
    progress == null ? "Progress unavailable" : `${Math.round(progress * 100)}% complete`,
  ] : [];
  $("journey-summary").textContent = details.join(" · ");
  $("journey-source").textContent = !f ? "" : f.details_source === "sample"
    ? "Illustrative trip · sample route, aircraft, and arrival time"
    : f.details_source === "aeroapi"
      ? "FlightAware API · estimated progress by elapsed flight time"
      : detailsState === "not_configured"
        ? "Live airports need a FlightAware AeroAPI key configured on your Python service."
        : detailsState === "unavailable"
          ? "Flight route API temporarily unavailable. The service will retry automatically."
          : detailsState === "not_requested"
            ? "Enable a flight details layer to request API route data."
            : "The API has no matching route or arrival estimate for this flight.";
  const collisions = overlaps(config.layout.elements);
  const clipped = config.layout.elements.filter(
    (e) =>
      e.visible &&
      !["logo", "progress"].includes(e.field) &&
      f &&
      (fieldText(e.field, f, config.layout.units).length * 4 - 1) * e.scale >
        e.width,
  );
  const notes = [];
  if (collisions.length)
    notes.push(
      `${collisions.length} overlapping layer ${collisions.length === 1 ? "pair" : "pairs"}. Later layers draw on top.`,
    );
  if (clipped.length)
    notes.push(
      `Text clips in: ${clipped.map((e) => labels[e.field]).join(", ")}.`,
    );
  $("layout-warning").hidden = !notes.length;
  $("layout-warning").textContent = notes.join(" ");
}
function renderHandles() {
  const focusId = document.activeElement?.dataset?.layerId;
  $("handles").replaceChildren();
  for (const e of config.layout.elements) {
    if (!e.visible) continue;
    const h = node(
      "button",
      null,
      `handle${e.id === selected ? " selected" : ""}`,
    );
    h.type = "button";
    h.dataset.layerId = e.id;
    h.setAttribute(
      "aria-label",
      `${labels[e.field]}, x ${e.x}, y ${e.y}. Drag or use arrow keys.`,
    );
    h.title = labels[e.field];
    Object.assign(h.style, {
      left: (e.x / 64) * 100 + "%",
      top: (e.y / 32) * 100 + "%",
      width: (e.width / 64) * 100 + "%",
      height: (e.height / 32) * 100 + "%",
    });
    h.addEventListener("pointerdown", (event) => {
      if (event.button !== 0) return;
      event.preventDefault();
      selected = e.id;
      renderLayers();
      renderInspector();
      for (const b of $("handles").children)
        b.classList.toggle("selected", b === h);
      h.focus();
      h.setPointerCapture(event.pointerId);
      drag = {
        id: e.id,
        startX: event.clientX,
        startY: event.clientY,
        x: e.x,
        y: e.y,
        snapshot: clone(config),
        moved: false,
      };
    });
    h.addEventListener("pointermove", (event) => {
      if (!drag || drag.id !== e.id) return;
      const r = $("matrix").getBoundingClientRect(),
        pos = moveElement(
          e,
          drag.x + ((event.clientX - drag.startX) * 64) / r.width,
          drag.y + ((event.clientY - drag.startY) * 32) / r.height,
        );
      if (pos.x === e.x && pos.y === e.y) return;
      drag.moved = true;
      e.x = pos.x;
      e.y = pos.y;
      h.style.left = (e.x / 64) * 100 + "%";
      h.style.top = (e.y / 32) * 100 + "%";
      renderInspector();
      paint();
    });
    const finish = () => {
      if (!drag) return;
      if (drag.moved) {
        undo.push(drag.snapshot);
        if (undo.length > 60) undo.shift();
        redo = [];
        dirty = true;
        persist();
        $("preset").value = "custom";
      }
      drag = null;
      renderInspector();
      renderHandles();
    };
    h.addEventListener("pointerup", finish);
    h.addEventListener("pointercancel", () => {
      if (drag) {
        config = drag.snapshot;
        drag = null;
        render();
      }
    });
    h.addEventListener("focus", () => {
      if (selected !== e.id) {
        selected = e.id;
        renderLayers();
        renderInspector();
        for (const b of $("handles").children)
          b.classList.toggle("selected", b === h);
      }
    });
    h.addEventListener("keydown", (event) => {
      const directions = {
        ArrowLeft: [-1, 0],
        ArrowRight: [1, 0],
        ArrowUp: [0, -1],
        ArrowDown: [0, 1],
      };
      if (directions[event.key]) {
        event.preventDefault();
        const [dx, dy] = directions[event.key],
          step = event.shiftKey ? 5 : 1;
        mutate(() =>
          Object.assign(e, moveElement(e, e.x + dx * step, e.y + dy * step)),
        );
      } else if (event.key === "Delete" || event.key === "Backspace") {
        event.preventDefault();
        mutate(() => (e.visible = false));
      }
    });
    $("handles").append(h);
  }
  if (focusId) {
    const h = [...$("handles").children].find(
      (n) => n.dataset.layerId === focusId,
    );
    h?.focus({ preventScroll: true });
  }
}
function renderFlights() {
  $("flights").replaceChildren();
  const f = currentFlight();
  $("flight-count").textContent = flights.length;
  $("flight-empty").hidden = flights.length > 0;
  $("flight-empty").textContent = ["stale", "unavailable"].includes(feedState)
    ? "No usable flight data right now. The service will retry automatically."
    : "No flights match these filters. Try a wider radius.";
  for (const flight of flights) {
    const tr = node("tr", null, flight.icao24 === f?.icao24 ? "active" : "");
    for (const text of [
      flight.callsign || flight.icao24,
      flight.airline_name,
      flight.altitude_ft == null
        ? "—"
        : `${Math.round(flight.altitude_ft).toLocaleString()} ft`,
      flight.speed_knots == null ? "—" : `${Math.round(flight.speed_knots)} kt`,
      `${(flight.distance_km / 1.852).toFixed(1)} NM`,
    ])
      tr.append(node("td", text));
    const td = node("td"),
      b = node(
        "button",
        flight.icao24 === f?.icao24 ? "Selected" : "Preview",
        "text-button",
      );
    b.setAttribute("aria-label", `Preview ${flight.callsign || flight.icao24}`);
    b.onclick = () => {
      flightId = flight.icao24;
      render();
      if (connected) refresh();
    };
    td.append(b);
    tr.append(td);
    $("flights").append(tr);
  }
}
function renderFilters() {
  const f = config.filters;
  for (const [id, k] of [
    ["latitude", "latitude"],
    ["longitude", "longitude"],
    ["radius", "radius_km"],
    ["min-altitude", "min_altitude_ft"],
    ["max-altitude", "max_altitude_ft"],
    ["min-speed", "min_speed_knots"],
    ["sort", "sort_by"],
    ["callsign-filter", "callsign"],
  ])
    $(id).value = f[k];
  $("airlines").value = f.airlines.join(", ");
  $("include-ground").checked = f.include_ground;
  $("radius-value").textContent = f.radius_km + " km";
}
function render() {
  if (!drag) {
    renderLayers();
    renderInspector();
    renderHandles();
  }
  paint();
  renderFlights();
  $("save").textContent = connected
    ? dirty
      ? "Save to display"
      : "Display saved"
    : "Save layout";
}

async function api(path, options = {}, connection = connected) {
  if (!connection) throw new Error("Connect your Python service first.");
  let response;
  try {
    response = await fetch(connection.url + path, {
      ...options,
      headers: {
        Authorization: "Bearer " + connection.token,
        ...(options.body ? { "Content-Type": "application/json" } : {}),
        ...options.headers,
      },
      signal: AbortSignal.timeout(25000),
    });
  } catch {
    throw new Error(
      "Service unreachable. Check the URL, HTTPS, and allowed editor origin.",
    );
  }
  let body;
  try {
    body = await response.json();
  } catch {
    throw new Error("This address did not return a Flightdeck API response.");
  }
  if (!response.ok) {
    const detail =
      typeof body.detail === "string"
        ? body.detail
        : response.status === 422
          ? "The service rejected the settings. Check your layout and filters."
          : `Service returned ${response.status}.`;
    throw new Error(detail);
  }
  return body;
}
async function refresh() {
  const serial = ++requestSerial;
  if (!connected) {
    flights = filterFlights(sampleFlights(config.filters, Date.now() / 1000, sampleEpoch), config.filters);
    feedState = "fresh";
    detailsState = "sample";
    $("feed-status").textContent = "Sample flights · no API key needed";
    $("feed-status").classList.remove("error");
    render();
    return;
  }
  $("refresh").disabled = true;
  try {
    const [result, status] = await Promise.all([
      api(`/api/preview?flight_id=${encodeURIComponent(flightId)}`, { method: "POST", body: JSON.stringify(config) }),
      api("/api/status"),
    ]);
    if (serial !== requestSerial) return;
    flights = result.flights;
    feedState = result.status;
    detailsState = result.details_status || "unknown";
    const source =
      result.source === "demo" ? "Service sample flights" : "OpenSky";
    $("source-badge").textContent =
      result.source === "demo" ? "SERVICE DEMO" : "LIVE SERVICE";
    $("feed-status").textContent =
      result.status === "unavailable"
        ? result.error
        : result.status === "stale"
          ? `${source} · stale data (${Math.round(result.cache_age_seconds)}s) · ${result.error}`
          : `${source} · ${result.flights.length} matching flights · cache ${Math.round(result.cache_age_seconds)}s old`;
    $("feed-status").classList.toggle(
      "error",
      ["unavailable", "stale"].includes(result.status),
    );
    const device = status.devices.find((d) => d.online) || status.devices[0];
    $("device-status").textContent = device
      ? `ESP32 ${device.id} · ${device.online ? "online" : "offline"} · acknowledged layout ${device.acknowledged_revision || "pending"}`
      : "ESP32 · waiting for first connection";
    render();
  } catch (error) {
    if (serial !== requestSerial) return;
    feedState = "unavailable";
    flights = [];
    $("feed-status").textContent = error.message;
    $("feed-status").classList.add("error");
    $("device-status").textContent = "ESP32 · service unreachable";
    render();
  } finally {
    if (serial === requestSerial) $("refresh").disabled = false;
  }
}
for (const key of ["x", "y", "width", "height", "scale"])
  $("element-" + key).addEventListener("change", () => {
    const e = element(),
      value = Number($("element-" + key).value);
    if (!Number.isInteger(value)) {
      renderInspector();
      return;
    }
    mutate(() => {
      const maxima = {
        x: 64 - e.width,
        y: 32 - e.height,
        width: 64 - e.x,
        height: 32 - e.y,
        scale: 3,
      };
      const minimum = ["x", "y"].includes(key) ? 0 : e.field === "progress" && ["width", "height"].includes(key) ? 7 : 1;
      e[key] = clamp(value, minimum, maxima[key]);
      if (key === "scale") e.height = Math.min(32 - e.y, 5 * e.scale);
    });
  });
$("element-color").addEventListener("change", () =>
  mutate(() => (element().color = $("element-color").value)),
);
$("units").onchange = () =>
  mutate(() => (config.layout.units = $("units").value));
$("brightness").oninput = () =>
  ($("brightness-value").textContent =
    `${Math.round((Number($("brightness").value) / 255) * 100)}%`);
$("brightness").onchange = () =>
  mutate(() => (config.layout.brightness = Number($("brightness").value)));
$("grid").onchange = paint;
new ResizeObserver(paint).observe($("matrix-wrap"));
$("preset").onchange = () => {
  const choice = $("preset").value;
  if (choice === "custom") return;
  mutate(() => {
    config.layout.elements = layoutPreset(defaults, choice);
    selected = config.layout.elements[0].id;
  });
  $("preset").value = choice;
};
function history(dir) {
  const from = dir === "undo" ? undo : redo,
    to = dir === "undo" ? redo : undo;
  if (!from.length) return;
  to.push(clone(config));
  config = from.pop();
  if (!config.layout.elements.some((e) => e.id === selected))
    selected = config.layout.elements[0].id;
  dirty = true;
  persist();
  renderFilters();
  refresh();
}
$("undo").onclick = () => history("undo");
$("redo").onclick = () => history("redo");
document.addEventListener("keydown", (event) => {
  if (["INPUT", "SELECT", "TEXTAREA"].includes(document.activeElement.tagName))
    return;
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "z") {
    event.preventDefault();
    history(event.shiftKey ? "redo" : "undo");
  }
});
$("next-flight").onclick = () => {
  if (!flights.length) return;
  const i = flights.findIndex((f) => f.icao24 === currentFlight()?.icao24);
  flightId = flights[(i + 1) % flights.length].icao24;
  render();
  if (connected) refresh();
};
let lastRotation = Date.now();
setInterval(() => {
  if (!document.hidden) paint();
  if (
    $("rotate").checked &&
    !document.hidden &&
    Date.now() - lastRotation >= config.rotation_seconds * 1000
  ) {
    lastRotation = Date.now();
    $("next-flight").click();
  }
}, 1000);
setInterval(() => {
  if (
    !document.hidden &&
    !drag &&
    !$("connection-dialog").open &&
    !["INPUT", "SELECT"].includes(document.activeElement.tagName)
  )
    refresh();
}, 10000);
$("refresh").onclick = refresh;
$("radius").oninput = () =>
  ($("radius-value").textContent = $("radius").value + " km");
$("filters-form").onsubmit = (event) => {
  event.preventDefault();
  const f = {
    ...config.filters,
    latitude: Number($("latitude").value),
    longitude: Number($("longitude").value),
    radius_km: Number($("radius").value),
    min_altitude_ft: Number($("min-altitude").value),
    max_altitude_ft: Number($("max-altitude").value),
    min_speed_knots: Number($("min-speed").value),
    airlines: [
      ...new Set(
        $("airlines")
          .value.toUpperCase()
          .split(",")
          .map((v) => v.trim())
          .filter(Boolean),
      ),
    ],
    callsign: $("callsign-filter").value.toUpperCase().trim(),
    include_ground: $("include-ground").checked,
    sort_by: $("sort").value,
  };
  try {
    validateConfig({ ...config, filters: f });
  } catch (error) {
    notify(error.message);
    return;
  }
  mutate(() => (config.filters = f));
  refresh();
  notify(
    connected
      ? "Preview filters applied. Save to display to update the ESP32."
      : "Flight filters applied.",
  );
};
$("save").onclick = async () => {
  try {
    validateConfig(config);
    if (connected) {
      $("save").disabled = true;
      const submitted = JSON.stringify(config);
      const result = await api("/api/config", {
        method: "PUT",
        headers: { "If-Match": String(revision) },
        body: submitted,
      });
      revision = result.revision;
      dirty = JSON.stringify(config) !== submitted;
      notify(
        `Layout ${revision} saved. The ESP32 picks it up on its next poll.`,
      );
    } else {
      const saved = persist();
      notify(
        saved
          ? "Layout saved in this browser. Export a copy to keep it."
          : "Browser storage is unavailable. Use Export layout to keep your work.",
      );
    }
    render();
  } catch (error) {
    notify(error.message);
  } finally {
    $("save").disabled = false;
  }
};
function download(blob, filename) {
  const url = URL.createObjectURL(blob),
    a = node("a");
  a.href = url;
  a.download = filename;
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}
$("export").onclick = () =>
  download(
    new Blob([JSON.stringify(config, null, 2) + "\n"], {
      type: "application/json",
    }),
    "flightdeck-layout.json",
  );
$("export-frame").onclick = () => {
  const c = document.createElement("canvas");
  c.width = 64;
  c.height = 32;
  const ctx = c.getContext("2d");
  renderPixels(config, currentFlight(), font, feedState).forEach((color, i) => {
    ctx.fillStyle = color;
    ctx.fillRect(i % 64, Math.floor(i / 64), 1, 1);
  });
  c.toBlob((blob) => {
    if (blob) download(blob, "flightdeck-preview.png");
  }, "image/png");
};
$("import-layout").onchange = async () => {
  const file = $("import-layout").files[0];
  if (!file) return;
  try {
    if (file.size > 524288)
      throw new Error("Layout files must be under 512 KB.");
    const next = validateConfig(upgradeJourneyDraft(validateConfig(JSON.parse(await file.text())), defaults, false));
    mutate(() => {
      config = clone(next);
      selected = config.layout.elements[0].id;
    });
    renderFilters();
    await refresh();
    notify("Layout imported.");
  } catch (error) {
    notify(error.message);
  } finally {
    $("import-layout").value = "";
  }
};
$("logo-upload").onchange = async () => {
  const file = $("logo-upload").files[0];
  if (!file) return;
  try {
    const code = currentFlight()?.airline_code;
    if (!code) throw new Error("Select a flight with an airline code first.");
    if (file.size > 5 * 1024 * 1024)
      throw new Error("Choose an image smaller than 5 MB.");
    if (!["image/png", "image/jpeg", "image/webp"].includes(file.type))
      throw new Error("Choose a PNG, JPEG, or WebP image.");
    const bitmap = await createImageBitmap(file),
      c = document.createElement("canvas");
    c.width = c.height = 28;
    const ctx = c.getContext("2d"),
      scale = Math.min(28 / bitmap.width, 28 / bitmap.height),
      w = Math.max(1, Math.round(bitmap.width * scale)),
      h = Math.max(1, Math.round(bitmap.height * scale));
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(
      bitmap,
      Math.floor((28 - w) / 2),
      Math.floor((28 - h) / 2),
      w,
      h,
    );
    bitmap.close();
    const bytes = ctx.getImageData(0, 0, 28, 28).data,
      pixels = [];
    for (let i = 0; i < bytes.length; i += 4)
      pixels.push(
        bytes[i + 3] < 64
          ? null
          : "#" +
              [bytes[i], bytes[i + 1], bytes[i + 2]]
                .map((v) => v.toString(16).padStart(2, "0"))
                .join(""),
      );
    const next = clone(config);
    next.logos[code] = { width: 28, height: 28, pixels };
    validateConfig(next);
    mutate(() => (config = next));
    notify(`Logo added for ${code}.`);
  } catch (error) {
    notify(error.message);
  } finally {
    $("logo-upload").value = "";
  }
};
$("restore-logo").onclick = () => {
  const code = currentFlight()?.airline_code;
  if (!defaults.logos[code]) return;
  try {
    const next = clone(config);
    next.logos[code] = clone(defaults.logos[code]);
    validateConfig(next);
    mutate(() => (config = next));
    notify(`Pixel logo restored for ${code}.`);
  } catch (error) {
    notify(error.message);
  }
};
$("remove-logo").onclick = () => {
  const code = currentFlight()?.airline_code;
  if (code) mutate(() => delete config.logos[code]);
};
$("connection-open").onclick = () => {
  $("service-url").value =
    connected?.url ||
    safeStorage.get("flightdeck.service-url") ||
    location.origin;
  $("connection-error").textContent = "";
  $("connection-dialog").showModal();
};
$("connection-close").onclick = () => $("connection-dialog").close();
$("connection-form").onsubmit = async (event) => {
  event.preventDefault();
  $("connection-error").textContent = "";
  const button = event.submitter;
  button.disabled = true;
  try {
    const url = new URL($("service-url").value);
    if (
      !["http:", "https:"].includes(url.protocol) ||
      url.username ||
      url.password ||
      url.search ||
      url.hash
    )
      throw new Error(
        "Use an HTTP or HTTPS origin without credentials or a query.",
      );
    if (url.pathname !== "/")
      throw new Error("Enter only the service origin, without an API path.");
    if (location.protocol === "https:" && url.protocol !== "https:")
      throw new Error(
        "Use an HTTPS service here, or open the editor directly from your local service.",
      );
    const candidate = { url: url.origin, token: $("admin-token").value.trim() };
    const result = await api("/api/config", {}, candidate);
    validateConfig(result.config);
    checkpoint();
    connected = candidate;
    revision = result.revision;
    config = validateConfig(upgradeJourneyDraft(result.config, defaults));
    selected = config.layout.elements[0].id;
    dirty = JSON.stringify(config) !== JSON.stringify(result.config);
    safeStorage.set("flightdeck.service-url", candidate.url);
    $("admin-token").value = "";
    $("source-badge").classList.add("live");
    $("connection-open").textContent = "Service settings";
    $("connection-dialog").close();
    persist();
    renderFilters();
    await refresh();
    notify("Connected. The saved display layout is loaded.");
  } catch (error) {
    $("connection-error").textContent = error.message;
  } finally {
    button.disabled = false;
  }
};
$("disconnect").onclick = () => {
  connected = null;
  requestSerial++;
  $("refresh").disabled = false;
  $("admin-token").value = "";
  $("connection-dialog").close();
  $("source-badge").textContent = "SAMPLE DATA";
  $("source-badge").classList.remove("live");
  $("connection-open").textContent = "Connect service ↗";
  $("device-status").textContent = "ESP32 · no service connected";
  refresh();
  notify("Sample mode is ready. Your layout is kept.");
};
renderFilters();
await refresh();
