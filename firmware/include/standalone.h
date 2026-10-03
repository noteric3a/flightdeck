#pragma once
// Portable device model and renderer: also compiled on the host for parity tests.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include "frame_protocol.h"
#include "font_data.h"

namespace flightdeck {
constexpr size_t kMaxLayoutBytes = 28000;
constexpr size_t kMaxFlights = 15;
enum Field { LOGO, CALLSIGN, AIRLINE, ALTITUDE, SPEED, DISTANCE, HEADING, VERTICAL_RATE, ROUTE, AIRCRAFT, ETA, PROGRESS };
struct Element { char id[25]{}; uint8_t field{}, x{}, y{}, width{}, height{}, scale{}, visible{}; uint16_t color{}; };
struct Logo { char code[4]{}; uint8_t width{}, height{}; const uint8_t* pixels = nullptr; };
struct Filters {
  double latitude{}, longitude{}, radius{}, minAltitude{}, maxAltitude{}, minSpeed{};
  uint16_t maxAge{}; bool ground{}, altitudeSort{}; char callsign[9]{}; uint8_t airlineCount{}; char airlines[50][4]{};
};
struct Layout {
  uint8_t brightness{}, metric{}; uint16_t background{}, rotation{};
  Filters filters; uint8_t elementCount{}, logoCount{}; Element elements[16]; Logo logos[50];
};
struct Flight {
  char id[96]{}, callsign[17]{}, airline[4]{}, airlineName[32]{}, origin[5]{}, destination[5]{}, aircraft[25]{};
  double latitude = NAN, longitude = NAN, altitude = NAN, speed = NAN, distance = NAN, heading = NAN, verticalRate = NAN;
  double departure = NAN, arrival = NAN, progress = NAN, positionTime = 0;
  bool ground = false, sample = false;
};
struct Reader {
  const uint8_t* bytes; size_t size, pos = 0; bool ok = true;
  uint8_t u8() { if (pos >= size) { ok = false; return 0; } return bytes[pos++]; }
  uint16_t u16() { uint16_t a = u8(); return a | (uint16_t(u8()) << 8); }
  double number() { uint8_t b[8]; for (auto& x : b) x = u8(); double n; memcpy(&n, b, 8); if (!std::isfinite(n)) ok = false; return n; }
  void text(char* out, size_t cap, size_t len) { if (len >= cap) { ok = false; return; } for (size_t i = 0; i < len; i++) out[i] = char(u8()); out[len] = 0; }
  bool flag() { uint8_t b = u8(); if (b > 1) ok = false; return b != 0; }
};
inline bool upperCode(const char* s, size_t n) { for (size_t i = 0; i < n; i++) if (s[i] < 'A' || s[i] > 'Z') return false; return s[n] == 0; }
inline bool parseLayout(const uint8_t* data, size_t size, Layout& output) {
  if (size < 65 || size > kMaxLayoutBytes || memcmp(data, "FDL2", 4)) return false;
  Layout l{}; Reader r{data, size}; r.pos = 4;
  l.brightness = r.u8(); l.metric = r.flag(); l.background = r.u16(); l.rotation = r.u16();
  auto& f = l.filters;
  f.latitude = r.number(); f.longitude = r.number(); f.radius = r.number(); f.minAltitude = r.number(); f.maxAltitude = r.number(); f.minSpeed = r.number();
  f.maxAge = r.u16(); f.ground = r.flag(); f.altitudeSort = r.flag(); r.text(f.callsign, sizeof(f.callsign), r.u8());
  if (!r.ok || !l.brightness || l.rotation < 5 || l.rotation > 300 || f.latitude < -90 || f.latitude > 90 || f.longitude < -180 || f.longitude > 180 || f.radius < 1 || f.radius > 250 || f.minAltitude < 0 || f.maxAltitude > 60000 || f.minAltitude > f.maxAltitude || f.minSpeed < 0 || f.minSpeed > 2000 || f.maxAge < 30 || f.maxAge > 600) return false;
  for (char c : f.callsign) if (c && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != ' ') return false;
  f.airlineCount = r.u8(); if (f.airlineCount > 50) return false;
  for (unsigned i = 0; i < f.airlineCount; i++) { r.text(f.airlines[i], 4, 3); if (!upperCode(f.airlines[i], 3)) return false; }
  l.elementCount = r.u8(); if (!l.elementCount || l.elementCount > 16) return false;
  for (unsigned i = 0; i < l.elementCount; i++) {
    auto& e = l.elements[i]; r.text(e.id, sizeof(e.id), r.u8());
    e.field = r.u8(); e.x = r.u8(); e.y = r.u8(); e.width = r.u8(); e.height = r.u8(); e.scale = r.u8(); e.visible = r.flag(); e.color = r.u16();
    if (!r.ok || !e.id[0] || e.field > PROGRESS || !e.width || !e.height || e.x + e.width > 64 || e.y + e.height > 32 || e.scale < 1 || e.scale > 3 || (e.field == PROGRESS && (e.width < 7 || e.height < 7))) return false;
    for (char c : e.id) if (c && c != '_' && !(c >= 'a' && c <= 'z')) return false;
    for (unsigned j = 0; j < i; j++) if (!strcmp(e.id, l.elements[j].id)) return false;
  }
  l.logoCount = r.u8(); if (l.logoCount > 50) return false;
  size_t pixels = 0;
  for (unsigned i = 0; i < l.logoCount; i++) {
    auto& logo = l.logos[i]; r.text(logo.code, 4, 3); logo.width = r.u8(); logo.height = r.u8();
    if (!upperCode(logo.code, 3) || !logo.width || logo.width > 32 || !logo.height || logo.height > 32) return false;
    for (unsigned j = 0; j < i; j++) if (!strcmp(logo.code, l.logos[j].code)) return false;
    size_t count = logo.width * logo.height; pixels += count;
    if (pixels > 8192 || r.pos + count * 3 > size) return false;
    logo.pixels = data + r.pos;
    for (size_t p = 0; p < count; p++) { r.u16(); r.flag(); }
  }
  if (!r.ok || r.pos != size) return false;
  output = l; return true;
}
inline double bounded(double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); }
inline double distanceKm(double a, double b, double c, double d) {
  const double rad = 3.141592653589793 / 180;
  const double n = bounded(pow(sin((c-a)*rad/2), 2) + cos(a*rad)*cos(c*rad)*pow(sin((d-b)*rad/2), 2), 0, 1);
  return 12742 * atan2(sqrt(n), sqrt(1-n));
}
inline bool matches(Flight& flight, const Filters& f, double now) {
  if (!std::isfinite(flight.latitude) || !std::isfinite(flight.longitude) || flight.latitude < -90 || flight.latitude > 90 || flight.longitude < -180 || flight.longitude > 180) return false;
  flight.distance = distanceKm(f.latitude, f.longitude, flight.latitude, flight.longitude);
  if (flight.distance > f.radius || (!f.ground && flight.ground) || now - flight.positionTime > f.maxAge || flight.positionTime > now + 60 || !strstr(flight.callsign, f.callsign)) return false;
  if (std::isfinite(flight.altitude) ? flight.altitude < f.minAltitude || flight.altitude > f.maxAltitude : f.minAltitude != 0 || f.maxAltitude != 60000) return false;
  if (std::isfinite(flight.speed) ? flight.speed < f.minSpeed : f.minSpeed != 0) return false;
  if (f.airlineCount) { bool found = false; for (unsigned i = 0; i < f.airlineCount; i++) found |= !strcmp(flight.airline, f.airlines[i]); if (!found) return false; }
  return true;
}
inline void fieldText(char* out, size_t size, unsigned field, const Flight& f, bool metric, double now) {
  double value = NAN; const char* suffix = "";
  switch (field) {
    case LOGO: snprintf(out, size, "%s", f.airline[0] ? f.airline : "---"); return;
    case CALLSIGN: snprintf(out, size, "%s", f.callsign[0] ? f.callsign : f.id); return;
    case AIRLINE: snprintf(out, size, "%s", f.airlineName[0] ? f.airlineName : f.airline[0] ? f.airline : "UNKNOWN"); return;
    case ROUTE: snprintf(out, size, "%s->%s", f.origin[0] ? f.origin : "---", f.destination[0] ? f.destination : "---"); return;
    case AIRCRAFT: snprintf(out, size, "%s", f.aircraft[0] ? f.aircraft : "--"); return;
    case ETA: {
      if (!std::isfinite(f.arrival) || f.arrival <= 0) { snprintf(out, size, "--"); return; }
      int minutes = int(std::max(0.0, ceil((f.arrival-now)/60)));
      if (minutes >= 6000) snprintf(out, size, ">99H");
      else if (minutes >= 60) snprintf(out, size, "%dH%02dM", minutes/60, minutes%60);
      else snprintf(out, size, "%dM", minutes);
      return;
    }
    case ALTITUDE: value = f.altitude * (metric ? .3048 : 1); suffix = metric ? "M" : "FT"; break;
    case SPEED: value = f.speed * (metric ? 1.852 : 1); suffix = metric ? "KMH" : "KT"; break;
    case DISTANCE: value = f.distance / (metric ? 1 : 1.852); suffix = metric ? "KM" : "NM"; break;
    case HEADING: value = f.heading; suffix = "D"; break;
    case VERTICAL_RATE: value = f.verticalRate * (metric ? .00508 : 1); suffix = metric ? "M/S" : "FPM"; break;
  }
  if (!std::isfinite(value)) snprintf(out, size, "--%s", suffix);
  else snprintf(out, size, "%.0f%s", floor(value + .5), suffix);
}
inline void put(uint16_t* pixels, int x, int y, uint16_t color) { if (x >= 0 && x < 64 && y >= 0 && y < 32) pixels[y*64+x] = color; }
inline void text(uint16_t* pixels, const char* s, const Element& e) {
  int cx = e.x;
  for (; *s; s++, cx += 4*e.scale) {
    if (cx + 3*e.scale > e.x + e.width) break;
    unsigned char c = *s; if (c >= 'a' && c <= 'z') c -= 32; if (c >= 128 || (!kGlyphs[c] && c != ' ')) c = '?';
    const uint16_t glyph = kGlyphs[c];
    for (int y = 0; y < 5; y++) for (int x = 0; x < 3; x++) if (glyph & (1 << (14-y*3-x)))
      for (int a = 0; a < e.scale; a++) for (int b = 0; b < e.scale; b++) if (y*e.scale+a < e.height) put(pixels, cx+x*e.scale+b, e.y+y*e.scale+a, e.color);
  }
}
inline void render(const Layout& l, const Flight* f, uint16_t* pixels, double now, bool stale = false) {
  std::fill(pixels, pixels+2048, l.background);
  if (!f) { Element e{}; e.x=2; e.y=13; e.width=60; e.height=5; e.scale=1; e.color=0x7cd5; text(pixels, stale ? "DATA OFFLINE" : "NO FLIGHTS", e); return; }
  for (unsigned i = 0; i < l.elementCount; i++) {
    const auto& e = l.elements[i]; if (!e.visible) continue;
    if (e.field == PROGRESS) {
      double p = f->progress / 100;
      if (std::isfinite(f->arrival) && f->arrival > 0 && std::isfinite(f->departure) && f->departure > 0 && f->arrival > f->departure) p = (now-f->departure)/(f->arrival-f->departure);
      int cy = e.y + e.height/2;
      if (!std::isfinite(p)) { Element unknown = e; unknown.y=cy-2; unknown.height=5; unknown.scale=1; unknown.color=0x7cd5; text(pixels, "--", unknown); continue; }
      int px = e.x + int(floor(bounded(p,0,1)*(e.width-7)+.5));
      for (int x = e.x; x < e.x+e.width; x++) put(pixels,x,cy,x < px+3 ? 0x07e0 : 0xffff);
      const uint8_t plane[] = {0x10,0x08,0x4c,0x7f,0x4c,0x08,0x10};
      for (int y = 0; y < 7; y++) for (int x = 0; x < 7; x++) put(pixels,px+x,cy-3+y,plane[y] & (1 << (6-x)) ? e.color : l.background);
      continue;
    }
    const Logo* logo = nullptr;
    if (e.field == LOGO) for (unsigned j = 0; j < l.logoCount; j++) if (!strcmp(l.logos[j].code, f->airline)) logo = &l.logos[j];
    if (logo) {
      for (unsigned y=0; y<e.height; y++) for (unsigned x=0; x<e.width; x++) {
        const uint8_t* p=logo->pixels+((y*logo->height/e.height)*logo->width+x*logo->width/e.width)*3;
        if (p[2]) put(pixels,e.x+x,e.y+y,read16(p));
      }
    } else {
      char value[128]; fieldText(value,sizeof(value),e.field,*f,l.metric,now); Element label=e;
      if (e.field == LOGO) { label.y += std::max(0,(int(e.height)-5)/2); label.scale=1; }
      text(pixels,value,label);
    }
  }
  if (stale) put(pixels,63,0,0xf5cc);
}
}
