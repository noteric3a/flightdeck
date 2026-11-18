#include "aeroapi.h"
#include "default_layout.h"
#include <cassert>
#include <cstdlib>
using namespace flightdeck;
void checkPerimeter(double latitude, double longitude, double radius) {
  Filters f{}; f.latitude=latitude; f.longitude=longitude; f.radius=radius;
  const auto box=nearbyBounds(f);
  const double rad=3.141592653589793/180, angular=radius/6371, lat=latitude*rad;
  assert(box.latMin >= -90 && box.latMax <= 90 && box.lonMin >= -180 && box.lonMax <= 180);
  for (unsigned bearing=0; bearing<360; bearing++) {
    const double heading=bearing*rad;
    const double pointLat=asin(sin(lat)*cos(angular)+cos(lat)*sin(angular)*cos(heading));
    const double pointLon=longitude+atan2(sin(heading)*sin(angular)*cos(lat),cos(angular)-sin(lat)*sin(pointLat))/rad;
    const double normalizedLon=fmod(pointLon+540,360)-180;
    assert(pointLat/rad >= box.latMin-1e-9 && pointLat/rad <= box.latMax+1e-9);
    assert(normalizedLon >= box.lonMin-1e-9 && normalizedLon <= box.lonMax+1e-9);
  }
}
int main() {
  setenv("TZ","UTC",1);tzset();
  checkPerimeter(40.4259,-86.9081,100);
  checkPerimeter(0,0,1);
  checkPerimeter(85,0,250);
  checkPerimeter(-85,0,250);
  checkPerimeter(89,40,250);
  checkPerimeter(-89,-40,250);
  checkPerimeter(0,179.9,100);
  checkPerimeter(0,-179.9,100);
  Filters polar{};polar.latitude=85;polar.radius=250;
  const auto polarBox=nearbyBounds(polar);
  assert(polarBox.lonMax >= 26.75115882964758 && polarBox.lonMin <= -26.75115882964758);
  polar.latitude=90;
  assert(nearbyBounds(polar).lonMin == -180 && nearbyBounds(polar).lonMax == 180);
  polar.latitude=-90;
  assert(nearbyBounds(polar).lonMin == -180 && nearbyBounds(polar).lonMax == 180);
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
