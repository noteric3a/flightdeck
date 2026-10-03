#include "aeroapi.h"
#include "default_layout.h"
#include <cassert>
#include <cstdlib>
using namespace flightdeck;
int main() {
  setenv("TZ","UTC",1);tzset();
  DynamicJsonDocument doc(4096);
  assert(!deserializeJson(doc,R"({"ident":"UA247","ident_icao":"UAL247","fa_flight_id":"UAL247-unique","origin":{"code_iata":"PHL"},"destination":{"code_icao":"KORD"},"aircraft_type":"B789","actual_off":"2026-10-03T16:00:00Z","actual_on":null,"last_position":{"latitude":40.5,"longitude":-86.8,"altitude":350,"groundspeed":430,"heading":null,"timestamp":"2026-10-03T17:00:00Z"}})"));
  Flight f;assert(parseFlight(doc.as<JsonObjectConst>(),f));
  assert(f.altitude==35000 && f.speed==430 && !std::isfinite(f.heading) && !std::isfinite(f.verticalRate));
  assert(!strcmp(f.airline,"UAL") && !strcmp(f.origin,"PHL") && !strcmp(f.destination,"KORD") && !strcmp(f.aircraft,"B787-9"));
  assert(!std::isfinite(f.arrival));
  Layout layout;assert(parseLayout(kDefaultLayout,sizeof(kDefaultLayout),layout));
  double now=timestamp("2026-10-03T17:00:00Z");assert(matches(f,layout.filters,now));
  assert(!matches(f,layout.filters,now+601));
  auto bad=f;bad.latitude=NAN;assert(!matches(bad,layout.filters,now));
  bad=f;bad.positionTime=now+61;assert(!matches(bad,layout.filters,now));
  assert(!deserializeJson(doc,R"({"flights":[{"fa_flight_id":"UAL247-wrong-day","actual_off":"2026-10-03T16:00:00Z","estimated_on":"2026-10-03T18:00:00Z"}]})"));
  assert(!enrichFlight(doc["flights"].as<JsonArrayConst>(),f));assert(!std::isfinite(f.arrival));
  doc["flights"][0]["fa_flight_id"]="UAL247-unique";
  assert(enrichFlight(doc["flights"].as<JsonArrayConst>(),f));assert(f.arrival==timestamp("2026-10-03T18:00:00Z"));
  doc["flights"][0]["estimated_on"]="2026-10-03T15:00:00Z";
  assert(enrichFlight(doc["flights"].as<JsonArrayConst>(),f));assert(!std::isfinite(f.arrival));
  doc["flights"][0]["diverted"]=true;
  assert(!enrichFlight(doc["flights"].as<JsonArrayConst>(),f));
  assert(!std::isfinite(timestamp(nullptr)) && !std::isfinite(timestamp("bad")));
}
