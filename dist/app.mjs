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
  rgb565,
} from "./core.mjs";
import { validateConfig } from "./validate.mjs";
import { layoutPreset, upgradeJourneyDraft, upgradeLegacyDraft } from "./presets.mjs";
import { journeyState, remainingText } from "./journey.mjs";
import { FlightdeckSerial, LivePreview } from "./serial.mjs";
import { encodeLayout, decodeLayout } from "./device-codec.mjs";
import { upgradeDisplayAssets } from "./asset-upgrades.mjs";

const $ = (id) => document.getElementById(id);
const sampleEpoch = Date.now() / 1000;
const [defaults, font] = await Promise.all(
  ["./defaults.json", "./font.json"].map(async (path) => {
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
  deviceInfo = null,
  feedState = "fresh",
  detailsState = "sample",
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
const device = new FlightdeckSerial();
const preview = new LivePreview(device, error => {
  $("live-preview").checked = false;
  notify(error.message);
});
let connecting = false, refreshing = false;
device.onDisconnect = () => {
  connected = null;
  preview.enabled = false;
  preview.pending = null;
  $("live-preview").checked = false;
  $("connection-open").textContent = "Connect ESP32";
  $("source-badge").classList.remove("live");
  $("source-badge").textContent = "SAMPLE DATA";
  $("device-status").textContent = "ESP32 · USB disconnected";
  $("include-ground").disabled = false;
  if (!connecting) refresh();
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
const assetUpgrade = upgradeDisplayAssets(config, defaults);
config = assetUpgrade.config;
if (assetUpgrade.changed) {
  dirty = true;
  persist();
  notify("Bundled logos and display colors updated. Save to ESP32 to apply.");
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
  preview.submit(rgb565(pixels), config.layout.brightness);
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
        ? "Set your FlightAware AeroAPI key in ESP32 settings."
        : detailsState === "unavailable"
          ? "Flight details are temporarily unavailable. The ESP32 will retry automatically."
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
    ? "No usable flight data right now. The ESP32 will retry automatically."
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
      ? "Save to ESP32"
      : "Saved to ESP32"
    : "Save layout";
}

async function refresh() {
  if (refreshing || connecting) return;
  if (!connected || !device.connected) {
    flights = filterFlights(sampleFlights(config.filters, Date.now() / 1000, sampleEpoch), config.filters);
    feedState = "fresh";
    detailsState = "sample";
    $("feed-status").textContent = "Sample flights · no API key needed";
    $("feed-status").classList.remove("error");
    render();
    return;
  }
  refreshing = true;
  $("refresh").disabled = true;
  try {
    const result = await device.command("status");
    if (!connected) return;
    deviceInfo = result;
    const clockOffset = result.mode === "demo" ? Date.now()/1000 - result.clock : 0;
    const received = (result.flights || []).map(f => {
      if (!clockOffset) return f;
      const adjusted = {...f};
      for (const key of ["position_time", "departure_time", "arrival_time"])
        if (Number.isFinite(f[key])) adjusted[key] += clockOffset;
      return adjusted;
    });
    flights = filterFlights(received, config.filters);
    feedState = result.stale ? "stale" : "fresh";
    detailsState = result.mode === "demo" ? "sample" : "aeroapi";
    $("source-badge").textContent = result.mode === "demo" ? "ESP32 DEMO" : "ESP32 LIVE";
    $("feed-status").textContent = `${result.message} · ${flights.length} flights · ${result.requests_this_hour}/${result.max_per_hour} requests this UTC hour`;
    $("feed-status").classList.toggle("error", !!result.stale);
    $("device-status").textContent = `ESP32 · USB connected · Wi-Fi ${result.wifi_connected ? result.ip : "offline"}${result.preview ? " · live preview" : " · autonomous display"}${result.panel_ready ? "" : " · PANEL INITIALIZATION FAILED"}${result.storage_ready ? "" : " · FLASH STORAGE UNAVAILABLE"}`;
    $("include-ground").disabled = result.mode === "live";
    render();
  } catch (error) {
    if (connected) { $("feed-status").textContent = error.message; $("feed-status").classList.add("error"); }
  } finally {
    refreshing = false;
    $("refresh").disabled = false;
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
  $("save").disabled = true;
  try {
    validateConfig(config);
    if (connected) {
      const submitted = JSON.stringify(config);
      const payload = encodeLayout(config);
      await preview.stop();
      $("live-preview").checked = false;
      await device.upload("layout", payload);
      dirty = JSON.stringify(config) !== submitted;
      notify("Saved on ESP32. It now renders this layout itself, including after restart.");
      await refresh();
    } else {
      const saved = persist();
      notify(saved ? "Layout saved in this browser. Connect ESP32 to save it on the board." : "Use Export layout to keep your work.");
    }
    render();
  } catch (error) { notify(error.message); }
  finally { $("save").disabled = false; }
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
function populateSettings() {
  const info = deviceInfo || {};
  $("wifi-ssid").value = info.ssid || "";
  $("wifi-password").value = "";
  $("aeroapi-key").value = "";
  $("open-network").checked = false;
  $("device-mode").value = info.mode || "demo";
  $("poll-seconds").value = info.poll_seconds || 300;
  $("request-limit").value = info.max_per_hour || 24;
  $("credential-state").textContent = `Wi-Fi password ${info.has_password ? "saved" : "not saved"} · AeroAPI key ${info.has_api_key ? "saved" : "not saved"}. Blank fields keep saved values.`;
  $("connection-error").textContent = "";
}
async function loadBoardLayout() {
  await preview.stop(); $("live-preview").checked = false;
  const upgrade = upgradeDisplayAssets(decodeLayout(await device.downloadLayout()), defaults);
  checkpoint(); config = upgrade.config; selected = config.layout.elements[0].id; dirty = upgrade.changed;
  persist(); renderFilters(); render();
  if (dirty) notify("Bundled logos and display colors updated. Save to ESP32 to apply.");
}
async function finishConnection(info) {
  deviceInfo = info; connected = device;
  const identity = device.port.getInfo();
  safeStorage.set("flightdeck.usb-device", JSON.stringify(identity));
  $("source-badge").classList.add("live");
  $("connection-open").textContent = "ESP32 settings";
  if (!dirty) await loadBoardLayout();
  populateSettings();
}
$("connection-open").onclick = async () => {
  if (connecting) return;
  if (device.connected) { populateSettings(); $("connection-dialog").showModal(); return; }
  connecting = true;
  try {
    const info = await device.choose();
    await finishConnection(info);
    $("connection-dialog").showModal();
    notify(dirty ? "ESP32 connected. Save to ESP32 to apply your updated layout." : "ESP32 connected over USB. Enable Live on matrix to preview edits.");
  } catch (error) { await device.close(); notify(error.name === "NotFoundError" ? "No USB port selected." : error.message); }
  finally { connecting = false; await refresh(); }
};
$("connection-close").onclick = () => $("connection-dialog").close();
$("connection-dialog").addEventListener("close", () => { $("wifi-password").value = ""; $("aeroapi-key").value = ""; });
$("connection-form").onsubmit = async event => {
  event.preventDefault();
  const button = event.submitter; button.disabled = true;
  try {
    const ssid = $("wifi-ssid").value;
    const fields = {ssid, mode: $("device-mode").value, poll_seconds: Number($("poll-seconds").value), max_per_hour: Number($("request-limit").value)};
    const password = $("wifi-password").value, apiKey = $("aeroapi-key").value.trim();
    if (password || $("open-network").checked) fields.password = $("open-network").checked ? "" : password;
    else if (ssid !== deviceInfo?.ssid) throw new Error("Enter the new network's password, or select Open network.");
    if (apiKey) fields.api_key = apiKey;
    deviceInfo = await device.command("configure", fields);
    $("wifi-password").value = ""; $("aeroapi-key").value = "";
    $("connection-dialog").close();
    notify("Settings saved on ESP32. Wi-Fi connects in the background.");
    await refresh();
  } catch (error) { $("connection-error").textContent = error.message; }
  finally { button.disabled = false; }
};
$("live-preview").onchange = async () => {
  try {
    if ($("live-preview").checked) {
      if (!device.connected) throw new Error("Connect your ESP32 first.");
      preview.enabled = true; paint();
    } else await preview.stop();
  } catch (error) { $("live-preview").checked = false; notify(error.message); }
};
$("load-board-layout").onclick = async () => {
  try { await loadBoardLayout(); await refresh(); notify(dirty ? "Board layout loaded with corrected logos and colors. Save to ESP32 to apply." : "Saved ESP32 layout loaded. Undo restores your prior draft."); }
  catch (error) { notify(error.message); }
};
$("forget-credentials").onclick = async () => {
  try {
    deviceInfo = await device.command("forget_credentials"); populateSettings(); await refresh();
    notify("Saved Wi-Fi and AeroAPI credentials cleared. ESP32 is in demo mode.");
  } catch (error) { $("connection-error").textContent = error.message; }
};
$("disconnect").onclick = async () => {
  try { await preview.stop(); } catch { /* Unplugged. */ }
  await device.close(); $("connection-dialog").close();
  notify("USB released. The powered ESP32 continues with its saved layout and mode.");
};
renderFilters();
await refresh();
// Only reconnect a previously selected port. Never probe arbitrary serial devices.
async function reconnectRemembered() {
  if (!device.serial || connecting || device.connected) return;
  let identity; try { identity = JSON.parse(safeStorage.get("flightdeck.usb-device")); } catch { return; }
  if (!identity || identity.usbVendorId == null) return;
  connecting = true;
  try {
    const ports = (await device.serial.getPorts()).filter(port => {
      const info = port.getInfo();
      return info.usbVendorId === identity.usbVendorId && info.usbProductId === identity.usbProductId;
    });
    if (ports.length === 1) await finishConnection(await device.open(ports[0]));
  } catch (error) { await device.close(); notify(error.message); }
  finally { connecting = false; await refresh(); }
}
if (device.serial) { device.serial.addEventListener("connect", reconnectRemembered); await reconnectRemembered(); }
else $("device-status").textContent = "USB setup requires desktop Chrome or Edge on HTTPS or localhost.";
