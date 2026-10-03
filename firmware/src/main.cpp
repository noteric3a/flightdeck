#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <mbedtls/base64.h>
#include <memory>
#include "config.h"
#include "standalone.h"
#include "aeroapi.h"
#include "default_layout.h"

using namespace flightdeck;
SET_LOOP_TASK_STACK_SIZE(24576);
extern const uint8_t trustBundle[] asm("_binary_certs_x509_crt_bundle_start");

struct Settings {
  char ssid[33]{}, password[65]{}, key[257]{};
  bool live = false;
  uint16_t pollSeconds = 300, maxPerHour = 24;
};
struct NetworkConfig { Settings settings; Filters filters; uint32_t generation; bool details; };
struct NetworkState {
  Flight flights[kMaxFlights]; uint8_t count = 0;
  bool wifi = false, truncated = false, success = false;
  char ip[20]{}, message[128] = "Demo mode";
  uint32_t generation = 0, fetchedAt = 0, retryIn = 0;
  uint16_t requests = 0;
};
Settings settings;
Layout layout;
const uint8_t* layoutData = kDefaultLayout;
size_t layoutSize = sizeof(kDefaultLayout);
std::unique_ptr<uint8_t[]> storedLayout;
MatrixPanel_I2S_DMA* matrix = nullptr;
bool panelReady = false, storageReady = false, prefsReady = false;
Preferences prefs;
QueueHandle_t configQueue = nullptr, stateQueue = nullptr;
NetworkState networkState;
uint32_t generation = 1, previewAt = 0, bootAt = 0;
bool previewActive = false;
uint16_t pixels[2048];
uint8_t displayed = 0;
uint32_t lastDraw = 0, lastRotate = 0;
struct Upload {
  std::unique_ptr<uint8_t[]> bytes;
  size_t size = 0, received = 0;
  uint32_t crc = 0, touched = 0;
  bool layout = false;
  void clear() { bytes.reset(); size = received = 0; }
} upload;

bool saveSettings(const Settings& value) {
  if (!prefsReady) return false;
  StaticJsonDocument<768> doc;
  doc["ssid"]=value.ssid; doc["password"]=value.password; doc["key"]=value.key;
  doc["live"]=value.live; doc["poll"]=value.pollSeconds; doc["limit"]=value.maxPerHour;
  String encoded; serializeJson(doc,encoded);
  return prefs.putString("settings",encoded) == encoded.length();
}
void loadSettings() {
  StaticJsonDocument<768> doc;
  if (!prefsReady || deserializeJson(doc,prefs.getString("settings",""))) return;
  copyText(settings.ssid,sizeof(settings.ssid),doc["ssid"] | "");
  copyText(settings.password,sizeof(settings.password),doc["password"] | "");
  copyText(settings.key,sizeof(settings.key),doc["key"] | "");
  settings.live=doc["live"] | false;
  settings.pollSeconds=constrain(doc["poll"] | 300,60,3600);
  settings.maxPerHour=constrain(doc["limit"] | 24,1,120);
}
bool loadLayoutFile(const char* name) {
  File file=LittleFS.open(name,"r"); if (!file || file.size()<69 || file.size()>kMaxLayoutBytes+4) return false;
  uint8_t checksum[4]; if (file.read(checksum,4)!=4) return false;
  size_t size=file.size()-4;
  std::unique_ptr<uint8_t[]> data(new(std::nothrow) uint8_t[size]); if (!data) return false;
  if (file.read(data.get(),size)!=size || crc32(data.get(),size)!=read32(checksum)) return false;
  Layout candidate; if (!parseLayout(data.get(),size,candidate)) return false;
  storedLayout=std::move(data); layoutData=storedLayout.get(); layoutSize=size; layout=candidate; return true;
}
bool saveLayoutFile(const uint8_t* data,size_t length) {
  if (!storageReady) return false;
  File file=LittleFS.open("/layout.tmp","w"); if (!file) return false;
  uint32_t crc=crc32(data,length); uint8_t header[]={uint8_t(crc),uint8_t(crc>>8),uint8_t(crc>>16),uint8_t(crc>>24)};
  bool ok=file.write(header,4)==4 && file.write(data,length)==length;
  file.flush(); file.close(); if (!ok) return false;
  // Keep a valid previous copy across interrupted writes and rename failures.
  if (LittleFS.exists("/layout.bin")) {
    LittleFS.remove("/layout.bak");
    if (!LittleFS.rename("/layout.bin","/layout.bak")) return false;
  }
  if (LittleFS.rename("/layout.tmp","/layout.bin")) return true;
  LittleFS.rename("/layout.bak","/layout.bin"); return false;
}
void sendNetworkConfig() {
  generation++; networkState=NetworkState{}; networkState.generation=generation;
  NetworkConfig config{}; config.settings=settings; config.filters=layout.filters; config.generation=generation;
  for (unsigned i=0;i<layout.elementCount;i++) config.details |= layout.elements[i].visible && (layout.elements[i].field==ETA || layout.elements[i].field==PROGRESS);
  if (configQueue) xQueueOverwrite(configQueue,&config);
}
String urlEncode(const String& value) {
  String out; out.reserve(value.length()*3);
  const char* hex="0123456789ABCDEF";
  for (unsigned char c : value) {
    if (isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~') out+=char(c);
    else { out+='%'; out+=hex[c>>4]; out+=hex[c&15]; }
  }
  return out;
}

// Streaming HTTP body reader with decoded-byte and total-time bounds. Supports
// chunked responses without buffering a provider response in the ESP32 heap.
class BoundedBody : public Stream {
  WiFiClient& input; bool chunked, ended=false; size_t remaining=0, bytes=0;
  int length; uint32_t started; int cached=-2;
  int raw() {
    while (!input.available()) {
      if (!input.connected() || millis()-started>15000) { ended=true; return -1; }
      delay(1);
    }
    return input.read();
  }
  int next() {
    if (ended || bytes>=131072 || millis()-started>15000) return -1;
    if (chunked && !remaining) {
      char line[40]; size_t n=0; int c;
      while ((c=raw())>=0 && c!='\n') { if (c!='\r') { if (n==sizeof(line)-1) { ended=true; return -1; } line[n++]=char(c); } }
      line[n]=0; if (c<0 || !n) { ended=true; return -1; }
      char* end; remaining=strtoul(line,&end,16);
      if (end==line || (*end && *end!=';') || !remaining || remaining>131072-bytes) { ended=true; return -1; }
    }
    if (!chunked && length>=0 && bytes>=size_t(length)) return -1;
    int c=raw(); if(c<0) return -1; bytes++;
    if (chunked && --remaining==0) { if(raw()!='\r' || raw()!='\n') ended=true; }
    return c;
  }
public:
  BoundedBody(WiFiClient& stream,bool chunks,int size):input(stream),chunked(chunks),length(size),started(millis()){}
  int read() override { if(cached!=-2) {int c=cached;cached=-2;return c;} return next(); }
  int peek() override { if(cached==-2) cached=next();return cached; }
  int available() override { return ended ? 0 : 1; }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }
};

struct Budget {
  Preferences store; uint64_t record=0; bool ready=false;
  Budget() { ready=store.begin("fd-budget",false); if(ready) record=store.getULong64("hour",0); }
  uint16_t count(time_t now) const { return (record>>16)==uint64_t(now/3600) ? uint16_t(record&65535) : 0; }
  bool take(time_t now,unsigned limit) {
    // Never reset the budget backwards if NTP jumps backwards.
    if(!ready || now<1700000000 || uint64_t(now/3600)<(record>>16)) return false;
    uint16_t used=count(now); if(used>=limit) return false;
    uint64_t next=(uint64_t(now/3600)<<16)|(used+1);
    if(store.putULong64("hour",next)!=8) return false;
    record=next; return true;
  }
};
uint32_t retryDelay(const String& header) {
  if(!header.length()) return 300;
  bool digits=true; for(char c:header) if(!isdigit(c)) digits=false;
  if(digits) return std::max(300UL,std::min(86400UL,strtoul(header.c_str(),nullptr,10)));
  struct tm t{};
  if(strptime(header.c_str(),"%a, %d %b %Y %H:%M:%S GMT",&t)) return uint32_t(bounded(difftime(mktime(&t),time(nullptr)),300,86400));
  return 300;
}
// Returns seconds until retry, zero on success. All requests, including detail
// enrichment and failed HTTP responses, consume the persisted UTC-hour budget.
uint32_t queryAeroAPI(const String& path,const Settings& s,Budget& budget,DynamicJsonDocument& doc,NetworkState& state,bool& authBlocked) {
  time_t now=time(nullptr);
  if(!budget.take(now,s.maxPerHour)) {copyText(state.message,sizeof(state.message),"Request cap reached (or budget storage unavailable)");return 3600-uint32_t(now%3600);}
  state.requests=budget.count(now);
  WiFiClientSecure client; client.setCACertBundle(trustBundle); client.setHandshakeTimeout(10);
  HTTPClient http; http.setConnectTimeout(5000); http.setTimeout(10000); http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if(!http.begin(client,"https://aeroapi.flightaware.com/aeroapi"+path)) {copyText(state.message,sizeof(state.message),"Could not initialize verified HTTPS");return 300;}
  const char* keys[]={"Retry-After","Transfer-Encoding","Content-Encoding"}; http.collectHeaders(keys,3);
  http.addHeader("x-apikey",s.key); http.addHeader("Accept","application/json"); http.addHeader("Accept-Encoding","identity");
  int code=http.GET();
  if(code!=200) {
    uint32_t wait=code==429 ? retryDelay(http.header("Retry-After")) : 300;
    authBlocked=code==401 || code==403;
    if(authBlocked) copyText(state.message,sizeof(state.message),"AeroAPI access denied; check key and plan in Studio");
    else snprintf(state.message,sizeof(state.message),"AeroAPI HTTP %d; retry scheduled",code);
    http.end();return wait;
  }
  if(http.getSize()>131072 || (http.header("Content-Encoding").length() && http.header("Content-Encoding")!="identity")) {
    copyText(state.message,sizeof(state.message),"AeroAPI response exceeds supported bounds");http.end();return 300;
  }
  StaticJsonDocument<2048> filter;
  auto f=filter["flights"].createNestedObject();
  for(const char* field:{"fa_flight_id","ident","ident_icao","aircraft_type","actual_off","actual_on","estimated_on","progress_percent","cancelled","diverted"}) f[field]=true;
  for(const char* field:{"latitude","longitude","altitude","groundspeed","heading","timestamp"}) f["last_position"][field]=true;
  for(const char* field:{"origin","destination"}) {f[field]["code_iata"]=true; f[field]["code_icao"]=true;}
  filter["links"]["next"]=true;
  BoundedBody stream(*http.getStreamPtr(),http.header("Transfer-Encoding").equalsIgnoreCase("chunked"),http.getSize());
  auto error=deserializeJson(doc,stream,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(12));
  http.end();
  if(error || !doc["flights"].is<JsonArray>()) {copyText(state.message,sizeof(state.message),"AeroAPI returned invalid or oversized JSON");return 300;}
  return 0;
}

void networkTask(void*) {
  // This task exclusively owns Wi-Fi, TLS and provider requests. USB/rendering
  // continues on the Arduino loop task even when DNS or TLS times out.
  NetworkConfig config{}; NetworkState state{};
  Budget budget; bool authBlocked=false; uint32_t due=0,lastWifi=0; unsigned detailIndex=0;
  struct Detail { char id[96]{}; double arrival=NAN,progress=NAN; uint32_t expires=0; } details[kMaxFlights];
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  configTime(0,0,"pool.ntp.org","time.nist.gov");
  for(;;) {
    NetworkConfig next;
    if(xQueueReceive(configQueue,&next,0)==pdTRUE) {
      bool wifiChanged=strcmp(next.settings.ssid,config.settings.ssid)||strcmp(next.settings.password,config.settings.password);
      bool keyChanged=strcmp(next.settings.key,config.settings.key);
      config=next; state=NetworkState{}; state.generation=config.generation;
      copyText(state.message,sizeof(state.message),config.settings.live ? "Waiting for next AeroAPI poll" : "Demo mode");
      if(keyChanged) {authBlocked=false; due=0; for(auto& d:details) d=Detail{};}
      if(wifiChanged) {WiFi.disconnect(false,true); if(config.settings.ssid[0]) WiFi.begin(config.settings.ssid,config.settings.password);lastWifi=millis();}
    }
    state.wifi=WiFi.status()==WL_CONNECTED; copyText(state.ip,sizeof(state.ip),state.wifi ? WiFi.localIP().toString().c_str() : "");
    time_t now=time(nullptr); state.requests=budget.count(now);
    if(!state.wifi && config.settings.ssid[0] && millis()-lastWifi>=15000) {WiFi.reconnect();lastWifi=millis();}
    if(!config.settings.live) copyText(state.message,sizeof(state.message),"Demo mode; no provider requests");
    else if(!config.settings.ssid[0]) copyText(state.message,sizeof(state.message),"Set Wi-Fi in Studio over USB");
    else if(!state.wifi) copyText(state.message,sizeof(state.message),"Wi-Fi disconnected; reconnecting");
    else if(!config.settings.key[0]) copyText(state.message,sizeof(state.message),"Set an AeroAPI key in Studio");
    else if(now<1700000000) copyText(state.message,sizeof(state.message),"Waiting for NTP clock before HTTPS");
    else if(authBlocked) copyText(state.message,sizeof(state.message),"AeroAPI access denied; update key or restart after fixing plan");
    else if(int32_t(millis()-due)>=0) {
      const auto& f=config.filters;
      // The bounding box contains the radius. Haversine filtering below removes
      // its corners. A dateline crossing uses full longitude (one capped page).
      double delta=f.radius/111.0, latMin=std::max(-90.0,f.latitude-delta),latMax=std::min(90.0,f.latitude+delta);
      double lonDelta=delta/std::max(0.001,cos(f.latitude*3.141592653589793/180));
      double lonMin=f.longitude-lonDelta,lonMax=f.longitude+lonDelta;
      if(lonMin < -180 || lonMax > 180 || latMin<=-90 || latMax>=90) {lonMin=-180;lonMax=180;}
      char query[200];snprintf(query,sizeof(query),"-latlong \"%.6f %.6f %.6f %.6f\"",latMin,lonMin,latMax,lonMax);
      String search(query);
      if(f.callsign[0] && !strchr(f.callsign,' ')) search+=" -idents *"+String(f.callsign)+"*";
      if(f.airlineCount==1) search+=" -airline "+String(f.airlines[0]);
      DynamicJsonDocument doc(24576);
      uint32_t retry=queryAeroAPI("/flights/search?max_pages=1&query="+urlEncode(search),config.settings,budget,doc,state,authBlocked);
      if(!retry) {
        state.count=0; state.truncated=!doc["links"]["next"].isNull();
        for(JsonObjectConst row:doc["flights"].as<JsonArrayConst>()) {
          Flight flight;
          if(!parseFlight(row,flight) || !matches(flight,f,double(now))) continue;
          if(state.count==kMaxFlights) {state.truncated=true;break;}
          for(const auto& d:details) if(!strcmp(d.id,flight.id) && d.expires>uint32_t(now)) {flight.arrival=d.arrival;flight.progress=d.progress;}
          state.flights[state.count++]=flight;
        }
        std::sort(state.flights,state.flights+state.count,[&](const Flight& a,const Flight& b){
          double av=f.altitudeSort ? (std::isfinite(a.altitude)?-a.altitude:1) : a.distance;
          double bv=f.altitudeSort ? (std::isfinite(b.altitude)?-b.altitude:1) : b.distance;
          return av==bv ? strcmp(a.id,b.id)<0 : av<bv;
        });
        state.fetchedAt=uint32_t(time(nullptr));state.success=true;
        copyText(state.message,sizeof(state.message),state.truncated ? "Live FlightAware; first page only (narrow radius if needed)" : "Live FlightAware");
        // Search supplies route/type/position, not estimated_on. Enrich at most
        // one rotating flight per poll; unknown ETA remains -- on other flights.
        if(config.details && state.count && budget.count(time(nullptr))<config.settings.maxPerHour) {
          auto& flight=state.flights[detailIndex++%state.count];
          if(!std::isfinite(flight.arrival)) {
            doc.clear();
            retry=queryAeroAPI("/flights/"+urlEncode(String(flight.id))+"?max_pages=1",config.settings,budget,doc,state,authBlocked);
            if(!retry && enrichFlight(doc["flights"].as<JsonArrayConst>(),flight)) {
              auto& d=details[(detailIndex-1)%kMaxFlights];copyText(d.id,sizeof(d.id),flight.id);d.arrival=flight.arrival;d.progress=flight.progress;d.expires=uint32_t(now)+600;
            }
          }
        }
      }
      due=millis()+std::max(uint32_t(config.settings.pollSeconds),retry)*1000;
    }
    state.retryIn=int32_t(due-millis())>0 ? (due-millis())/1000 : 0;
    xQueueOverwrite(stateQueue,&state);
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void showPixels(uint8_t brightness) {
  if(!panelReady) return;
  matrix->setBrightness8(brightness);
  for(int y=0;y<32;y++) for(int x=0;x<64;x++) matrix->drawPixel(x,y,pixels[y*64+x]);
  matrix->flipDMABuffer();
}
double modelNow() {
  time_t now=time(nullptr);
  return now>=1700000000 ? double(now) : 1800000000.0+double(millis())/1000;
}
Flight demo(unsigned index) {
  const char* ids[]={"UAL247","DAL1082","SWA516","AAL903","N625EC","UAL700"};
  const char* codes[]={"UAL","DAL","SWA","AAL","PVT","UAL"};
  const char* names[]={"United","Delta","Southwest","American","Private","United"};
  const char* origins[]={"PHL","ATL","MDW","DFW","","ORD"};
  const char* destinations[]={"ORD","JFK","DEN","LAX","","SFO"};
  const char* types[]={"B787-9","E175","B737-800","A321","C172","B737-800"};
  const int before[]={510,75,35,130,-1,-1},after[]={225,45,100,25,-1,-1};
  Flight f;index%=6;
  copyText(f.id,sizeof(f.id),ids[index]);copyText(f.callsign,sizeof(f.callsign),ids[index]);copyText(f.airline,sizeof(f.airline),codes[index]);copyText(f.airlineName,sizeof(f.airlineName),names[index]);
  copyText(f.origin,sizeof(f.origin),origins[index]);copyText(f.destination,sizeof(f.destination),destinations[index]);copyText(f.aircraft,sizeof(f.aircraft),types[index]);
  double now=modelNow(),epoch=now-double(millis()-bootAt)/1000;
  f.latitude=layout.filters.latitude+.025*(index+1);f.longitude=layout.filters.longitude+.015*index;
  f.altitude=index==5?0:28000+index*1000;f.speed=index==5?15:430+index*8;f.heading=85+index*12;f.verticalRate=0;
  f.positionTime=now;f.ground=index==5;f.sample=true;
  if(before[index]>=0) {f.departure=epoch-before[index]*60;f.arrival=epoch+after[index]*60;}
  return f;
}
size_t visibleFlights(Flight* out,bool& stale) {
  size_t count=0;double now=modelNow();stale=false;
  if(!settings.live) {for(unsigned i=0;i<6;i++) {auto f=demo(i);if(matches(f,layout.filters,now)) out[count++]=f;}return count;}
  stale=!networkState.success || !networkState.wifi || now-networkState.fetchedAt>settings.pollSeconds+30;
  for(unsigned i=0;i<networkState.count;i++) {auto f=networkState.flights[i];if(matches(f,layout.filters,now)) out[count++]=f;}
  if(!count && networkState.count) stale=true;
  return count;
}
void drawStandalone() {
  Flight flights[kMaxFlights];bool stale;size_t count=visibleFlights(flights,stale);
  if(count) displayed%=count;else displayed=0;
  render(layout,count?&flights[displayed]:nullptr,pixels,modelNow(),stale);
  showPixels(layout.brightness);
}
void addFlight(JsonObject row,const Flight& f) {
  row["icao24"]=f.id;row["callsign"]=f.callsign;row["airline_code"]=f.airline;row["airline_name"]=f.airlineName;
  row["latitude"]=f.latitude;row["longitude"]=f.longitude;row["altitude_ft"]=f.altitude;row["speed_knots"]=f.speed;row["heading"]=f.heading;row["vertical_rate_fpm"]=f.verticalRate;
  row["distance_km"]=f.distance;row["on_ground"]=f.ground;row["position_time"]=f.positionTime;
  row["departure_airport"]=f.origin;row["arrival_airport"]=f.destination;row["aircraft_type"]=f.aircraft;
  row["departure_time"]=f.departure;row["arrival_time"]=f.arrival;row["progress_percent"]=f.progress;row["details_source"]=f.sample?"sample":"aeroapi";
}
void addStatus(JsonDocument& response) {
  response["clock"]=modelNow();
  response["product"]="flightdeck";response["protocol"]=2;response["firmware"]="2.0.0";
  response["width"]=64;response["height"]=32;response["panel_ready"]=panelReady;response["storage_ready"]=storageReady&&prefsReady;
  response["ssid"]=settings.ssid;response["has_password"]=bool(settings.password[0]);response["has_api_key"]=bool(settings.key[0]);
  response["mode"]=settings.live?"live":"demo";response["poll_seconds"]=settings.pollSeconds;response["max_per_hour"]=settings.maxPerHour;
  response["wifi_connected"]=networkState.wifi;response["ip"]=networkState.ip;response["message"]=networkState.message;
  response["requests_this_hour"]=networkState.requests;response["retry_in"]=networkState.retryIn;response["preview"]=previewActive;
  response["layout_crc"]=crc32(layoutData,layoutSize);response["truncated"]=networkState.truncated;response["free_heap"]=ESP.getFreeHeap();
}
void command(char* line) {
  StaticJsonDocument<2048> request;
  if(deserializeJson(request,line)) return;
  if(!request["id"].is<uint32_t>() || !request["cmd"].is<const char*>()) return;
  const char* cmd=request["cmd"];DynamicJsonDocument response(!strcmp(cmd,"status")?20000:2048);
  response["id"]=request["id"];response["ok"]=true;
  auto fail=[&](const char* message){response["ok"]=false;response["error"]=message;};
  if(!strcmp(cmd,"hello") || !strcmp(cmd,"status")) {
    addStatus(response);
    if(!strcmp(cmd,"status")) {Flight flights[kMaxFlights];bool stale;size_t count=visibleFlights(flights,stale);response["stale"]=stale;auto rows=response.createNestedArray("flights");for(size_t i=0;i<count;i++) addFlight(rows.createNestedObject(),flights[i]);}
  } else if(!strcmp(cmd,"configure")) {
    Settings next=settings;bool valid=true;
    if(request.containsKey("ssid")) {const char* s=request["ssid"];if(!s || strlen(s)>32) valid=false;else copyText(next.ssid,sizeof(next.ssid),s);}
    if(request.containsKey("password")) {const char* s=request["password"];if(!s || strlen(s)>64 || (strlen(s)>0&&strlen(s)<8)) valid=false;else {if(strlen(s)==64) for(const char* p=s;*p;p++) if(!isxdigit(*p)) valid=false;copyText(next.password,sizeof(next.password),s);}}
    if(request.containsKey("api_key")) {const char* s=request["api_key"];if(!s || strlen(s)>256) valid=false;else {for(const char* p=s;*p;p++) if(*p<33 || *p>126) valid=false;copyText(next.key,sizeof(next.key),s);}}
    if(request.containsKey("mode")) {const char* s=request["mode"]|"";if(strcmp(s,"live")&&strcmp(s,"demo")) valid=false;else next.live=!strcmp(s,"live");}
    if(request.containsKey("poll_seconds")) {if(!request["poll_seconds"].is<unsigned>() || request["poll_seconds"].as<unsigned>()<60 || request["poll_seconds"].as<unsigned>()>3600) valid=false;else next.pollSeconds=request["poll_seconds"];}
    if(request.containsKey("max_per_hour")) {if(!request["max_per_hour"].is<unsigned>() || request["max_per_hour"].as<unsigned>()<1 || request["max_per_hour"].as<unsigned>()>120) valid=false;else next.maxPerHour=request["max_per_hour"];}
    if(next.live && (!next.key[0] || !next.ssid[0])) valid=false;
    if(!valid) fail("Invalid settings. Live mode needs Wi-Fi and an AeroAPI key.");
    else if(!saveSettings(next)) fail("Could not save settings to flash.");
    else {settings=next;sendNetworkConfig();addStatus(response);}
  } else if(!strcmp(cmd,"forget_credentials")) {
    Settings next; if(!saveSettings(next)) fail("Could not clear saved credentials.");else {settings=next;sendNetworkConfig();addStatus(response);}
  } else if(!strcmp(cmd,"upload_begin")) {
    upload.clear();const char* kind=request["kind"]|"";
    unsigned size=request["length"]|0u;
    if(!request["length"].is<unsigned>() || !request["crc"].is<uint32_t>() || (!strcmp(kind,"layout") ? size<65 || size>kMaxLayoutBytes : strcmp(kind,"frame") || size!=4097)) fail("Invalid upload type or size.");
    else {upload.bytes.reset(new(std::nothrow) uint8_t[size]);if(!upload.bytes) fail("Not enough free memory for upload.");else {upload.size=size;upload.crc=request["crc"];upload.layout=!strcmp(kind,"layout");upload.touched=millis();}}
  } else if(!strcmp(cmd,"upload_chunk")) {
    const char* encoded=request["data"]|"";uint8_t bytes[384];size_t got=0;
    if(!upload.bytes || !request["offset"].is<unsigned>() || request["offset"].as<unsigned>()!=upload.received || !strlen(encoded) || strlen(encoded)>512 || mbedtls_base64_decode(bytes,sizeof(bytes),&got,reinterpret_cast<const unsigned char*>(encoded),strlen(encoded)) || !got || got>upload.size-upload.received) {upload.clear();fail("Invalid upload chunk or offset.");}
    else {memcpy(upload.bytes.get()+upload.received,bytes,got);upload.received+=got;upload.touched=millis();}
  } else if(!strcmp(cmd,"upload_commit")) {
    if(!upload.bytes || upload.received!=upload.size || crc32(upload.bytes.get(),upload.size)!=upload.crc) fail("Incomplete upload or checksum mismatch.");
    else if(upload.layout) {
      Layout candidate;
      if(!parseLayout(upload.bytes.get(),upload.size,candidate)) fail("Invalid layout.");
      else if(!saveLayoutFile(upload.bytes.get(),upload.size)) fail("Could not save layout; previous layout retained.");
      else {storedLayout=std::move(upload.bytes);layoutData=storedLayout.get();layoutSize=upload.size;layout=candidate;previewActive=false;displayed=0;sendNetworkConfig();drawStandalone();response["layout_crc"]=crc32(layoutData,layoutSize);}
    } else if(!upload.bytes[0]) fail("Invalid brightness.");
    else {for(size_t i=0;i<2048;i++) pixels[i]=read16(upload.bytes.get()+1+i*2);showPixels(upload.bytes[0]);previewActive=true;previewAt=millis();}
    upload.clear();
  } else if(!strcmp(cmd,"upload_abort")) upload.clear();
  else if(!strcmp(cmd,"resume")) {previewActive=false;drawStandalone();}
  else if(!strcmp(cmd,"layout_info")) {response["length"]=layoutSize;response["crc"]=crc32(layoutData,layoutSize);}
  else if(!strcmp(cmd,"layout_chunk")) {
    unsigned offset=request["offset"]|unsigned(layoutSize);
    if(!request["offset"].is<unsigned>() || offset>=layoutSize) fail("Invalid layout offset.");
    else {char b64[513];size_t written;mbedtls_base64_encode(reinterpret_cast<unsigned char*>(b64),sizeof(b64),&written,layoutData+offset,std::min(size_t(384),layoutSize-offset));b64[written]=0;response["data"]=b64;}
  } else fail("Unknown command.");
  if(response.overflowed()) {response.clear();response["id"]=request["id"];fail("Response memory limit exceeded.");}
  // Passwords and API keys are never included in replies or debug output.
  serializeJson(response,Serial);Serial.write('\n');
}
void serviceSerial() {
  static char line[1200];static size_t used=0;static bool discard=false;static uint32_t touched=0;
  if(used && millis()-touched>5000) {used=0;discard=false;}
  unsigned work=0;
  while(Serial.available() && work++<2048) {
    char c=Serial.read();touched=millis();
    if(c=='\n') {if(!discard && used) {line[used]=0;command(line);}used=0;discard=false;}
    else if(c!='\r' && !discard) {if(used<sizeof(line)-1) line[used++]=c;else {used=0;discard=true;}}
  }
  if(upload.bytes && millis()-upload.touched>10000) upload.clear();
}
void setup() {
  Serial.begin(115200);
  bootAt=millis();
  // No wait for Serial: a USB host is optional after provisioning.
  prefsReady=prefs.begin("flightdeck",false);loadSettings();
  storageReady=LittleFS.begin(false);
  // Format only on initial provisioning, never erase a previously mounted
  // filesystem automatically after a flash or power failure.
  if(!storageReady && prefsReady && !prefs.getBool("fsready",false)) {
    storageReady=LittleFS.format() && LittleFS.begin(false);
  }
  if(storageReady && prefsReady) prefs.putBool("fsready",true);
  parseLayout(kDefaultLayout,sizeof(kDefaultLayout),layout);
  if(storageReady && !loadLayoutFile("/layout.bin")) loadLayoutFile("/layout.bak");
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  HUB75_I2S_CFG cfg(PANEL_WIDTH,PANEL_HEIGHT,1);
  cfg.gpio.e=9;cfg.clkphase=false;cfg.driver=HUB75_I2S_CFG::FM6126A;
#else
  HUB75_I2S_CFG::i2s_pins pins={PIN_R1,PIN_G1,PIN_B1,PIN_R2,PIN_G2,PIN_B2,PIN_A,PIN_B,PIN_C,PIN_D,-1,PIN_LAT,PIN_OE,PIN_CLK};
  HUB75_I2S_CFG cfg(PANEL_WIDTH,PANEL_HEIGHT,1,pins);
#endif
  cfg.double_buff=true;cfg.setPixelColorDepthBits(6);
  matrix=new MatrixPanel_I2S_DMA(cfg);panelReady=matrix->begin();
  configQueue=xQueueCreate(1,sizeof(NetworkConfig));stateQueue=xQueueCreate(1,sizeof(NetworkState));
  if(configQueue && stateQueue) {
    // Both supported boards are dual-core. Stack accommodates bounded JSON and
    // the flight snapshots; network data stays off the display task's stack.
    if(xTaskCreatePinnedToCore(networkTask,"flightaware",24576,nullptr,1,nullptr,0)!=pdPASS) {vQueueDelete(configQueue);vQueueDelete(stateQueue);configQueue=stateQueue=nullptr;}
  }
  sendNetworkConfig();drawStandalone();
}
void loop() {
  serviceSerial();
  if(stateQueue) {static NetworkState incoming;if(xQueueReceive(stateQueue,&incoming,0)==pdTRUE && incoming.generation==generation) networkState=incoming;}
  if(!configQueue) copyText(networkState.message,sizeof(networkState.message),"Network task allocation failed; restart with lower panel depth");
  uint32_t now=millis();
  if(previewActive && now-previewAt>4000) {previewActive=false;lastDraw=0;}
  if(!previewActive && now-lastDraw>=1000) {
    if(now-lastRotate>=uint32_t(layout.rotation)*1000) {displayed++;lastRotate=now;}
    drawStandalone();lastDraw=now;
  }
  delay(1);
}
