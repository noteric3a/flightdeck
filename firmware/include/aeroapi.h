#pragma once
#include <ArduinoJson.h>
#include <time.h>
#include "standalone.h"

namespace flightdeck {
inline double timestamp(const char* s) {
  if (!s || strlen(s) != 20 || s[19] != 'Z') return NAN;
  struct tm t{};
  if (!strptime(s, "%Y-%m-%dT%H:%M:%SZ", &t)) return NAN;
  // Firmware timezone is always UTC (configTime(0,0,...)).
  time_t value = mktime(&t);
  return value > 0 ? double(value) : NAN;
}
inline void copyText(char* target, size_t capacity, const char* value) {
  snprintf(target, capacity, "%s", value ? value : "");
}
inline double jsonNumber(JsonVariantConst v) { return v.is<double>() ? v.as<double>() : NAN; }
inline void airport(char* dest, JsonVariantConst value) {
  const char* code = value["code_iata"] | value["code_icao"] | "";
  size_t n = strlen(code);
  if (n < 3 || n > 4) return;
  for (size_t i = 0; i < n; i++) if (!(code[i] >= 'A' && code[i] <= 'Z') && !(code[i] >= '0' && code[i] <= '9')) return;
  copyText(dest, 5, code);
}
inline void aircraftName(char* dest, const char* code) {
  if (!code) return;
  const char* from[] = {"B788", "B789", "B78X", "B738", "E75L", "E75S"};
  const char* to[] = {"B787-8", "B787-9", "B787-10", "B737-800", "E175", "E175"};
  for (int i=0; i<6; i++) if (!strcmp(code, from[i])) { code=to[i]; break; }
  copyText(dest,25,code);
}
inline bool parseFlight(JsonObjectConst row, Flight& f) {
  const char* id = row["fa_flight_id"] | "";
  const char* ident = row["ident_icao"] | row["ident"] | "";
  if (!id[0] || strlen(id) >= sizeof(f.id) || !ident[0] || strlen(ident) >= sizeof(f.callsign)) return false;
  copyText(f.id,sizeof(f.id),id); copyText(f.callsign,sizeof(f.callsign),ident);
  // ICAO commercial designators have three letters followed by a flight number.
  if (strlen(ident) >= 4 && isupper(ident[0]) && isupper(ident[1]) && isupper(ident[2]) && isdigit(ident[3])) memcpy(f.airline,ident,3);
  copyText(f.airlineName,sizeof(f.airlineName),f.airline);
  const char* codes[] = {"UAL","DAL","SWA","AAL"}; const char* names[] = {"United","Delta","Southwest","American"};
  for (int i=0;i<4;i++) if (!strcmp(f.airline,codes[i])) copyText(f.airlineName,sizeof(f.airlineName),names[i]);
  auto p = row["last_position"];
  f.latitude=jsonNumber(p["latitude"]); f.longitude=jsonNumber(p["longitude"]);
  f.altitude=jsonNumber(p["altitude"])*100; f.speed=jsonNumber(p["groundspeed"]); f.heading=jsonNumber(p["heading"]);
  f.positionTime=timestamp(p["timestamp"] | static_cast<const char*>(nullptr));
  if (!std::isfinite(f.positionTime)) return false;
  f.ground = !row["actual_on"].isNull();
  f.departure=timestamp(row["actual_off"] | static_cast<const char*>(nullptr));
  airport(f.origin,row["origin"]); airport(f.destination,row["destination"]); aircraftName(f.aircraft,row["aircraft_type"] | "");
  return true;
}
inline bool enrichFlight(JsonArrayConst rows, Flight& f) {
  for (JsonObjectConst row : rows) {
    if (strcmp(row["fa_flight_id"] | "", f.id) || (row["cancelled"] | false) || (row["diverted"] | false) || !row["actual_on"].isNull()) continue;
    const double dep=timestamp(row["actual_off"] | static_cast<const char*>(nullptr));
    const double arr=timestamp(row["estimated_on"] | static_cast<const char*>(nullptr));
    if (std::isfinite(dep)) f.departure=dep;
    f.arrival=std::isfinite(arr) && arr > f.departure ? arr : NAN;
    const double progress=jsonNumber(row["progress_percent"]);
    f.progress=progress >= 0 && progress <= 100 ? progress : NAN;
    return true;
  }
  return false;
}
}
