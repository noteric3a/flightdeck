// =============================================================================
// Flightdeck firmware: main program
//
// Big picture of what this board does:
//   1. On power-up, load saved settings (Wi-Fi, API key) and the saved screen
//      layout, then start the LED matrix.
//   2. A background "network task" connects to Wi-Fi and periodically asks
//      FlightAware (AeroAPI) which flights are near you.
//   3. The main loop draws one flight at a time on the 64x32 LED panel, and
//      rotates to the next flight every few seconds.
//   4. While plugged into a computer, Flightdeck Studio can talk to the board
//      over USB (serial) to change settings, upload layouts, or show previews.
// =============================================================================

// Arduino basics (Serial, millis, delay, etc.)
#include <Arduino.h>
// Reading and writing JSON text
#include <ArduinoJson.h>
// Wi-Fi and secure (HTTPS) web requests
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
// "Preferences" = small key/value storage in flash (survives power-off)
#include <Preferences.h>
// LittleFS = a small file system in flash, used to store the layout file
#include <LittleFS.h>
// Driver for the HUB75 LED matrix panel
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
// Base64 encode/decode, used to send binary data over the text-based USB link
#include <mbedtls/base64.h>
#include <memory>
// Our own files: pin numbers, the drawing code, FlightAware parsing, and the
// built-in default layout that is used when nothing has been saved yet.
#include "config.h"
#include "standalone.h"
#include "aeroapi.h"
#include "default_layout.h"

using namespace flightdeck;
// Give the main loop a bigger memory stack, because it handles JSON messages.
SET_LOOP_TASK_STACK_SIZE(24576);
// A list of trusted website certificates, built into the firmware. It lets the
// board check that it is really talking to FlightAware over HTTPS.
extern const uint8_t trustBundle[] asm("_binary_certs_x509_crt_bundle_start");

// User settings that are saved to flash: Wi-Fi name/password, API key, whether
// to use real ("live") data or fake demo flights, how often to check for
// flights, and the maximum number of FlightAware requests allowed per hour.
struct Settings {
  char ssid[33]{}, password[65]{}, key[257]{};
  bool live = false;
  uint16_t pollSeconds = 300, maxPerHour = 24;
};
// The "instructions" the main program hands to the network task: settings,
// which flights to look for (filters), and a version number ("generation").
struct NetworkConfig { Settings settings; Filters filters; uint32_t generation; bool details; };
// The "report" the network task hands back: the flights it found, whether
// Wi-Fi is connected, the IP address, a status message, and request counts.
struct NetworkState {
  Flight flights[kMaxFlights]; uint8_t count = 0;
  bool wifi = false, truncated = false, success = false;
  char ip[20]{}, message[128] = "Demo mode";
  uint32_t generation = 0, fetchedAt = 0, retryIn = 0;
  uint16_t requests = 0;
};
// ----- Global state (shared by the whole program) -----

// The current user settings.
Settings settings;
// The current screen layout (where each piece of text/logo goes, colors, etc.)
Layout layout;
// The raw bytes of the current layout. Starts as the built-in default.
const uint8_t* layoutData = kDefaultLayout;
size_t layoutSize = sizeof(kDefaultLayout);
// Memory that holds a layout loaded from flash or uploaded from Studio.
std::unique_ptr<uint8_t[]> storedLayout;
// The LED matrix driver object.
MatrixPanel_I2S_DMA* matrix = nullptr;
// Flags that say whether the panel, file system and preferences started OK.
bool panelReady = false, storageReady = false, prefsReady = false;
Preferences prefs;
// Two "mailboxes" for passing data between the main loop and network task:
// configQueue sends instructions out, stateQueue brings results back.
QueueHandle_t configQueue = nullptr, stateQueue = nullptr;
// The latest results received from the network task.
NetworkState networkState;
// generation: bumps every time settings change, so old results get ignored.
// previewAt: when a Studio preview image was shown. bootAt: when we started.
uint32_t generation = 1, previewAt = 0, bootAt = 0;
// True while a preview image from Studio is on screen.
bool previewActive = false;
// The picture to show: 64 x 32 = 2048 pixels, each a 16-bit RGB565 color.
uint16_t pixels[2048];
// Which flight (from the list) is currently on screen.
uint8_t displayed = 0;
// When we last redrew the screen, and when we last moved to the next flight.
uint32_t lastDraw = 0, lastRotate = 0;
// Holds a file that Studio is sending over USB, piece by piece. It can be a
// new layout, or a single preview "frame" (picture) to show briefly.
struct Upload {
  std::unique_ptr<uint8_t[]> bytes;
  size_t size = 0, received = 0;
  uint32_t crc = 0, touched = 0;
  bool layout = false;
  void clear() { bytes.reset(); size = received = 0; }
} upload;

// ----- Saving and loading settings and layouts -----

// Save the settings to flash. They are packed into one JSON text string.
// Returns true if the save worked.
bool saveSettings(const Settings& value) {
  if (!prefsReady) return false;
  StaticJsonDocument<768> doc;
  doc["ssid"]=value.ssid; doc["password"]=value.password; doc["key"]=value.key;
  doc["live"]=value.live; doc["poll"]=value.pollSeconds; doc["limit"]=value.maxPerHour;
  String encoded; serializeJson(doc,encoded);
  return prefs.putString("settings",encoded) == encoded.length();
}
// Read the saved settings back from flash. If nothing is saved (or it can't be
// read), keep the defaults. Numbers are clamped to safe ranges, e.g. the
// poll interval must be between 60 seconds and 1 hour.
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
// Load a layout file from flash (e.g. "/layout.bin").
// The file starts with a 4-byte checksum (CRC32), followed by the layout data.
// The checksum is a "fingerprint" that lets us detect a damaged file.
// Returns true only if the file exists, isn't damaged, and is a valid layout.
bool loadLayoutFile(const char* name) {
  // Open the file and make sure its size is reasonable.
  File file=LittleFS.open(name,"r"); if (!file || file.size()<69 || file.size()>kMaxLayoutBytes+4) return false;
  // Read the 4-byte checksum at the start.
  uint8_t checksum[4]; if (file.read(checksum,4)!=4) return false;
  size_t size=file.size()-4;
  // Make room in memory for the layout data.
  std::unique_ptr<uint8_t[]> data(new(std::nothrow) uint8_t[size]); if (!data) return false;
  // Read the data and check that its fingerprint matches the saved one.
  if (file.read(data.get(),size)!=size || crc32(data.get(),size)!=read32(checksum)) return false;
  // Make sure the data is a valid layout before using it.
  Layout candidate; if (!parseLayout(data.get(),size,candidate)) return false;
  // Everything checked out: switch to this layout.
  storedLayout=std::move(data); layoutData=storedLayout.get(); layoutSize=size; layout=candidate; return true;
}
// Save a layout to flash as "/layout.bin", in a crash-safe way:
//   1. write everything to a temporary file "/layout.tmp"
//   2. rename the old "/layout.bin" to "/layout.bak" (a backup)
//   3. rename the temporary file to "/layout.bin"
// If power is lost part-way, there is always one good copy left.
bool saveLayoutFile(const uint8_t* data,size_t length) {
  if (!storageReady) return false;
  File file=LittleFS.open("/layout.tmp","w"); if (!file) return false;
  // Compute the checksum and split it into 4 bytes to write first.
  uint32_t crc=crc32(data,length); uint8_t header[]={uint8_t(crc),uint8_t(crc>>8),uint8_t(crc>>16),uint8_t(crc>>24)};
  // Write the checksum, then the layout data.
  bool ok=file.write(header,4)==4 && file.write(data,length)==length;
  file.flush(); file.close(); if (!ok) return false;
  // Keep a valid previous copy across interrupted writes and rename failures.
  if (LittleFS.exists("/layout.bin")) {
    LittleFS.remove("/layout.bak");
    if (!LittleFS.rename("/layout.bin","/layout.bak")) return false;
  }
  // Put the new file in place. If that fails, restore the backup.
  if (LittleFS.rename("/layout.tmp","/layout.bin")) return true;
  LittleFS.rename("/layout.bak","/layout.bin"); return false;
}
// Tell the network task about new settings or filters.
// Bumps the "generation" number so any results based on the old settings are
// thrown away, and clears the current flight list.
void sendNetworkConfig() {
  generation++; networkState=NetworkState{}; networkState.generation=generation;
  NetworkConfig config{}; config.settings=settings; config.filters=layout.filters; config.generation=generation;
  // Only ask FlightAware for extra per-flight details (arrival time, progress)
  // if the layout actually shows the ETA or the progress bar. Saves requests.
  for (unsigned i=0;i<layout.elementCount;i++) config.details |= layout.elements[i].visible && (layout.elements[i].field==ETA || layout.elements[i].field==PROGRESS);
  // Drop the instructions in the mailbox (replacing any unread ones).
  if (configQueue) xQueueOverwrite(configQueue,&config);
}

// ----- Talking to FlightAware (AeroAPI) over the internet -----

// Make text safe to put in a web address. For example a space becomes "%20".
// Letters, digits and - _ . ~ are left as-is; everything else is encoded.
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
//
// In plain terms: FlightAware's reply can be large, and the ESP32 has little
// memory. Instead of downloading the whole reply first, this reads it one
// character at a time and hands it straight to the JSON reader. It also acts
// as a safety guard: it stops after 128 KB of data or 15 seconds.
// "Chunked" replies arrive in pieces, each starting with its size; this class
// strips those size markers out so the JSON reader only sees the real data.
class BoundedBody : public Stream {
  WiFiClient& input; bool chunked, ended=false; size_t remaining=0, bytes=0;
  int length; uint32_t started; int cached=-2;
  // Read one raw byte from the network, waiting for it if needed.
  // Gives up (returns -1) if the connection closes or 15 seconds pass.
  int raw() {
    while (!input.available()) {
      if (!input.connected() || millis()-started>15000) { ended=true; return -1; }
      delay(1);
    }
    return input.read();
  }
  // Return the next real data byte, handling chunk markers and the limits.
  int next() {
    if (ended || bytes>=131072 || millis()-started>15000) return -1;
    // At the start of each chunk, read the line that says how big it is
    // (written in hexadecimal), and reject anything that looks wrong.
    if (chunked && !remaining) {
      char line[40]; size_t n=0; int c;
      while ((c=raw())>=0 && c!='\n') { if (c!='\r') { if (n==sizeof(line)-1) { ended=true; return -1; } line[n++]=char(c); } }
      line[n]=0; if (c<0 || !n) { ended=true; return -1; }
      char* end; remaining=strtoul(line,&end,16);
      if (end==line || (*end && *end!=';') || !remaining || remaining>131072-bytes) { ended=true; return -1; }
    }
    // For normal (non-chunked) replies, stop once we've read the full length.
    if (!chunked && length>=0 && bytes>=size_t(length)) return -1;
    int c=raw(); if(c<0) return -1; bytes++;
    // At the end of a chunk there must be a line break ("\r\n").
    if (chunked && --remaining==0) { if(raw()!='\r' || raw()!='\n') ended=true; }
    return c;
  }
public:
  BoundedBody(WiFiClient& stream,bool chunks,int size):input(stream),chunked(chunks),length(size),started(millis()){}
  // read() takes the next byte; peek() looks at it without taking it.
  int read() override { if(cached!=-2) {int c=cached;cached=-2;return c;} return next(); }
  int peek() override { if(cached==-2) cached=next();return cached; }
  int available() override { return ended ? 0 : 1; }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }
};

// Keeps count of how many FlightAware requests were made in the current hour,
// so we never go over the user's "max per hour" limit (AeroAPI costs money).
// The count is saved in flash so a reboot doesn't reset it.
// It's stored as one number: the hour (top part) + the count (bottom 16 bits).
struct Budget {
  Preferences store; uint64_t record=0; bool ready=false;
  Budget() { ready=store.begin("fd-budget",false); if(ready) record=store.getULong64("hour",0); }
  // How many requests were used so far this hour (0 if it's a new hour).
  uint16_t count(time_t now) const { return (record>>16)==uint64_t(now/3600) ? uint16_t(record&65535) : 0; }
  // Try to use one request from the budget. Returns false if we're at the
  // limit, the clock isn't set yet, or the count couldn't be saved.
  bool take(time_t now,unsigned limit) {
    // Never reset the budget backwards if NTP jumps backwards.
    if(!ready || now<1700000000 || uint64_t(now/3600)<(record>>16)) return false;
    uint16_t used=count(now); if(used>=limit) return false;
    uint64_t next=(uint64_t(now/3600)<<16)|(used+1);
    if(store.putULong64("hour",next)!=8) return false;
    record=next; return true;
  }
};
// When FlightAware says "too many requests", it may include a Retry-After
// header saying how long to wait (either a number of seconds or a date).
// This turns that into seconds, kept between 5 minutes and 1 day.
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
//
// In plain terms: make one HTTPS request to FlightAware and put the JSON reply
// into "doc". If anything goes wrong, write a message into state.message and
// return how many seconds to wait before trying again.
uint32_t queryAeroAPI(const String& path,const Settings& s,Budget& budget,DynamicJsonDocument& doc,NetworkState& state,bool& authBlocked) {
  time_t now=time(nullptr);
  // Stop if this hour's request limit has been used up.
  if(!budget.take(now,s.maxPerHour)) {copyText(state.message,sizeof(state.message),"Request cap reached (or budget storage unavailable)");return 3600-uint32_t(now%3600);}
  state.requests=budget.count(now);
  // Set up a secure (HTTPS) connection that checks FlightAware's certificate,
  // with timeouts so a bad connection can't hang forever.
  WiFiClientSecure client; client.setCACertBundle(trustBundle); client.setHandshakeTimeout(10);
  HTTPClient http; http.setConnectTimeout(5000); http.setTimeout(10000); http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if(!http.begin(client,"https://aeroapi.flightaware.com/aeroapi"+path)) {copyText(state.message,sizeof(state.message),"Could not initialize verified HTTPS");return 300;}
  // Remember a few reply headers we care about.
  const char* keys[]={"Retry-After","Transfer-Encoding","Content-Encoding"}; http.collectHeaders(keys,3);
  // Send the API key, and ask for plain (uncompressed) JSON.
  http.addHeader("x-apikey",s.key); http.addHeader("Accept","application/json"); http.addHeader("Accept-Encoding","identity");
  // Send the request. "code" is the HTTP status (200 = OK).
  int code=http.GET();
  if(code!=200) {
    // 429 = too many requests: wait as long as FlightAware asks.
    // Anything else: wait 5 minutes.
    uint32_t wait=code==429 ? retryDelay(http.header("Retry-After")) : 300;
    // 401/403 = the API key is wrong or not allowed. Stop trying until fixed.
    authBlocked=code==401 || code==403;
    if(authBlocked) copyText(state.message,sizeof(state.message),"AeroAPI access denied; check key and plan in Studio");
    else snprintf(state.message,sizeof(state.message),"AeroAPI HTTP %d; retry scheduled",code);
    http.end();return wait;
  }
  // Refuse replies that are too big or compressed (we can't handle those).
  if(http.getSize()>131072 || (http.header("Content-Encoding").length() && http.header("Content-Encoding")!="identity")) {
    copyText(state.message,sizeof(state.message),"AeroAPI response exceeds supported bounds");http.end();return 300;
  }
  // Build a "filter" listing only the JSON fields we need. The JSON reader
  // skips everything else, which saves a lot of memory.
  StaticJsonDocument<2048> filter;
  auto f=filter["flights"].createNestedObject();
  for(const char* field:{"fa_flight_id","ident","ident_icao","aircraft_type","actual_off","actual_on","estimated_on","progress_percent","cancelled","diverted"}) f[field]=true;
  for(const char* field:{"latitude","longitude","altitude","groundspeed","heading","timestamp"}) f["last_position"][field]=true;
  for(const char* field:{"origin","destination"}) {f[field]["code_iata"]=true; f[field]["code_icao"]=true;}
  filter["links"]["next"]=true;
  // Read the reply through our size/time-limited reader and parse the JSON.
  BoundedBody stream(*http.getStreamPtr(),http.header("Transfer-Encoding").equalsIgnoreCase("chunked"),http.getSize());
  auto error=deserializeJson(doc,stream,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(12));
  http.end();
  // The reply must contain a "flights" list, or we treat it as a failure.
  if(error || !doc["flights"].is<JsonArray>()) {copyText(state.message,sizeof(state.message),"AeroAPI returned invalid or oversized JSON");return 300;}
  return 0;
}

// The background network task. It runs forever on its own CPU core, so slow
// Wi-Fi or internet requests never freeze the LED display or USB connection.
// Each time around its loop it:
//   1. checks for new instructions (settings) from the main program,
//   2. keeps Wi-Fi connected,
//   3. when it's time, asks FlightAware for nearby flights,
//   4. sends the results back to the main program.
void networkTask(void*) {
  // This task exclusively owns Wi-Fi, TLS and provider requests. USB/rendering
  // continues on the Arduino loop task even when DNS or TLS times out.
  NetworkConfig config{}; NetworkState state{};
  Budget budget; bool authBlocked=false; uint32_t due=0,lastWifi=0; unsigned detailIndex=0;
  // A small memory of extra details (arrival time, progress) we already
  // fetched for specific flights, so we don't pay to fetch them again.
  struct Detail { char id[96]{}; double arrival=NAN,progress=NAN; uint32_t expires=0; } details[kMaxFlights];
  // Start Wi-Fi in normal "join a network" mode with auto-reconnect.
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  // Get the real time of day from internet time servers (needed for HTTPS).
  configTime(0,0,"pool.ntp.org","time.nist.gov");
  for(;;) {
    // 1. Check the mailbox for new settings from the main program.
    NetworkConfig next;
    if(xQueueReceive(configQueue,&next,0)==pdTRUE) {
      bool wifiChanged=strcmp(next.settings.ssid,config.settings.ssid)||strcmp(next.settings.password,config.settings.password);
      bool keyChanged=strcmp(next.settings.key,config.settings.key);
      config=next; state=NetworkState{}; state.generation=config.generation;
      copyText(state.message,sizeof(state.message),config.settings.live ? "Waiting for next AeroAPI poll" : "Demo mode");
      // New API key: forget old errors/cached details and try right away.
      if(keyChanged) {authBlocked=false; due=0; for(auto& d:details) d=Detail{};}
      // New Wi-Fi name/password: disconnect and join the new network.
      if(wifiChanged) {WiFi.disconnect(false,true); if(config.settings.ssid[0]) WiFi.begin(config.settings.ssid,config.settings.password);lastWifi=millis();}
    }
    // 2. Record whether Wi-Fi is connected, and our IP address.
    state.wifi=WiFi.status()==WL_CONNECTED; copyText(state.ip,sizeof(state.ip),state.wifi ? WiFi.localIP().toString().c_str() : "");
    time_t now=time(nullptr); state.requests=budget.count(now);
    // If Wi-Fi dropped, try to reconnect (at most every 15 seconds).
    if(!state.wifi && config.settings.ssid[0] && millis()-lastWifi>=15000) {WiFi.reconnect();lastWifi=millis();}
    // 3. Work out whether we can ask FlightAware for flights right now.
    // Each check below explains why not (shown in Studio as the status).
    if(!config.settings.live) copyText(state.message,sizeof(state.message),"Demo mode; no provider requests");
    else if(!config.settings.ssid[0]) copyText(state.message,sizeof(state.message),"Set Wi-Fi in Studio over USB");
    else if(!state.wifi) copyText(state.message,sizeof(state.message),"Wi-Fi disconnected; reconnecting");
    else if(!config.settings.key[0]) copyText(state.message,sizeof(state.message),"Set an AeroAPI key in Studio");
    else if(now<1700000000) copyText(state.message,sizeof(state.message),"Waiting for NTP clock before HTTPS");
    else if(authBlocked) copyText(state.message,sizeof(state.message),"AeroAPI access denied; update key or restart after fixing plan");
    // Everything is ready, and it's time for the next check.
    else if(int32_t(millis()-due)>=0) {
      const auto& f=config.filters;
      // The bounding box contains the radius. Haversine filtering below removes
      // its corners. A dateline crossing uses full longitude (one capped page).
      //
      // In plain terms: build a rectangle on the map around your location
      // (1 degree of latitude is about 111 km). Flights in the rectangle's
      // corners, outside the actual circle, get removed later.
      double delta=f.radius/111.0, latMin=std::max(-90.0,f.latitude-delta),latMax=std::min(90.0,f.latitude+delta);
      double lonDelta=delta/std::max(0.001,cos(f.latitude*3.141592653589793/180));
      double lonMin=f.longitude-lonDelta,lonMax=f.longitude+lonDelta;
      if(lonMin < -180 || lonMax > 180 || latMin<=-90 || latMax>=90) {lonMin=-180;lonMax=180;}
      // Turn the rectangle (plus optional callsign/airline filters) into a
      // FlightAware search query.
      char query[200];snprintf(query,sizeof(query),"-latlong \"%.6f %.6f %.6f %.6f\"",latMin,lonMin,latMax,lonMax);
      String search(query);
      if(f.callsign[0] && !strchr(f.callsign,' ')) search+=" -idents *"+String(f.callsign)+"*";
      if(f.airlineCount==1) search+=" -airline "+String(f.airlines[0]);
      // Ask FlightAware. "retry" is 0 if it worked, otherwise seconds to wait.
      DynamicJsonDocument doc(24576);
      uint32_t retry=queryAeroAPI("/flights/search?max_pages=1&query="+urlEncode(search),config.settings,budget,doc,state,authBlocked);
      if(!retry) {
        state.count=0; state.truncated=!doc["links"]["next"].isNull();
        // Go through each flight in the reply.
        for(JsonObjectConst row:doc["flights"].as<JsonArrayConst>()) {
          Flight flight;
          // Skip flights we can't read, or that don't match the filters.
          if(!parseFlight(row,flight) || !matches(flight,f,double(now))) continue;
          // Stop if our list is full.
          if(state.count==kMaxFlights) {state.truncated=true;break;}
          // Re-use any arrival/progress details we fetched earlier.
          for(const auto& d:details) if(!strcmp(d.id,flight.id) && d.expires>uint32_t(now)) {flight.arrival=d.arrival;flight.progress=d.progress;}
          state.flights[state.count++]=flight;
        }
        // Sort flights: highest first (if sorting by altitude), otherwise
        // closest first. Ties are broken by flight ID so the order is stable.
        std::sort(state.flights,state.flights+state.count,[&](const Flight& a,const Flight& b){
          double av=f.altitudeSort ? (std::isfinite(a.altitude)?-a.altitude:1) : a.distance;
          double bv=f.altitudeSort ? (std::isfinite(b.altitude)?-b.altitude:1) : b.distance;
          return av==bv ? strcmp(a.id,b.id)<0 : av<bv;
        });
        // Remember when this worked, for the "data is old" check later.
        state.fetchedAt=uint32_t(time(nullptr));state.success=true;
        copyText(state.message,sizeof(state.message),state.truncated ? "Live FlightAware; first page only (narrow radius if needed)" : "Live FlightAware");
        // Search supplies route/type/position, not estimated_on. Enrich at most
        // one rotating flight per poll; unknown ETA remains -- on other flights.
        //
        // In plain terms: the search reply doesn't include arrival times. If
        // the layout needs them (ETA / progress bar), fetch them for ONE flight
        // per check, taking turns, to keep the number of paid requests low.
        if(config.details && state.count && budget.count(time(nullptr))<config.settings.maxPerHour) {
          auto& flight=state.flights[detailIndex++%state.count];
          if(!std::isfinite(flight.arrival)) {
            doc.clear();
            retry=queryAeroAPI("/flights/"+urlEncode(String(flight.id))+"?max_pages=1",config.settings,budget,doc,state,authBlocked);
            // Save the details for 10 minutes (600 seconds) so they can be re-used.
            if(!retry && enrichFlight(doc["flights"].as<JsonArrayConst>(),flight)) {
              auto& d=details[(detailIndex-1)%kMaxFlights];copyText(d.id,sizeof(d.id),flight.id);d.arrival=flight.arrival;d.progress=flight.progress;d.expires=uint32_t(now)+600;
            }
          }
        }
      }
      // Schedule the next check: the normal poll interval, or longer if
      // FlightAware asked us to wait.
      due=millis()+std::max(uint32_t(config.settings.pollSeconds),retry)*1000;
    }
    // 4. Send the results back to the main program, then rest for 1/4 second.
    state.retryIn=int32_t(due-millis())>0 ? (due-millis())/1000 : 0;
    xQueueOverwrite(stateQueue,&state);
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

// ----- Drawing on the LED panel -----

// Copy our 64x32 picture ("pixels") onto the LED panel.
// The panel uses two screen buffers: we draw into the hidden one, then "flip"
// so the new picture appears all at once without flicker.
void showPixels(uint8_t brightness) {
  if(!panelReady) return;
  matrix->setBrightness8(brightness);
  for(int y=0;y<32;y++) for(int x=0;x<64;x++) matrix->drawPixel(x,y,pixels[y*64+x]);
  matrix->flipDMABuffer();
}
// The current time in seconds. If the real clock isn't set yet (no internet),
// use a made-up time instead so demo flights still look sensible.
double modelNow() {
  time_t now=time(nullptr);
  return now>=1700000000 ? double(now) : 1800000000.0+double(millis())/1000;
}
// Make one of 6 fake demo flights (used when not in "live" mode).
// "before"/"after" = minutes since departure / until arrival (-1 = none).
Flight demo(unsigned index) {
  const char* ids[]={"UAL247","DAL1082","SWA516","AAL903","N625EC","UAL700"};
  const char* codes[]={"UAL","DAL","SWA","AAL","PVT","UAL"};
  const char* names[]={"United","Delta","Southwest","American","Private","United"};
  const char* origins[]={"PHL","ATL","MDW","DFW","","ORD"};
  const char* destinations[]={"ORD","JFK","DEN","LAX","","SFO"};
  const char* types[]={"B787-9","E175","B737-800","A321","C172","B737-800"};
  const int before[]={510,75,35,130,-1,-1},after[]={225,45,100,25,-1,-1};
  Flight f;index%=6;
  // Fill in the flight's name, airline, airports and aircraft type.
  copyText(f.id,sizeof(f.id),ids[index]);copyText(f.callsign,sizeof(f.callsign),ids[index]);copyText(f.airline,sizeof(f.airline),codes[index]);copyText(f.airlineName,sizeof(f.airlineName),names[index]);
  copyText(f.origin,sizeof(f.origin),origins[index]);copyText(f.destination,sizeof(f.destination),destinations[index]);copyText(f.aircraft,sizeof(f.aircraft),types[index]);
  // "epoch" = roughly when the board started, so demo times stay fixed.
  double now=modelNow(),epoch=now-double(millis()-bootAt)/1000;
  // Place each demo flight a little away from your location, with made-up
  // altitude, speed and heading. Flight #5 is parked on the ground.
  f.latitude=layout.filters.latitude+.025*(index+1);f.longitude=layout.filters.longitude+.015*index;
  f.altitude=index==5?0:28000+index*1000;f.speed=index==5?15:430+index*8;f.heading=85+index*12;f.verticalRate=0;
  f.positionTime=now;f.ground=index==5;f.sample=true;
  if(before[index]>=0) {f.departure=epoch-before[index]*60;f.arrival=epoch+after[index]*60;}
  return f;
}
// Fill "out" with the flights that should be shown right now (demo flights or
// live ones), keeping only those that match the filters. Returns how many.
// Also sets "stale" to true if the live data is old or Wi-Fi is down.
size_t visibleFlights(Flight* out,bool& stale) {
  size_t count=0;double now=modelNow();stale=false;
  // Demo mode: use the 6 fake flights.
  if(!settings.live) {for(unsigned i=0;i<6;i++) {auto f=demo(i);if(matches(f,layout.filters,now)) out[count++]=f;}return count;}
  // Live mode: data is "stale" if the last check failed, Wi-Fi is down, or
  // it's been longer than the poll interval (+30 s) since the last update.
  stale=!networkState.success || !networkState.wifi || now-networkState.fetchedAt>settings.pollSeconds+30;
  for(unsigned i=0;i<networkState.count;i++) {auto f=networkState.flights[i];if(matches(f,layout.filters,now)) out[count++]=f;}
  if(!count && networkState.count) stale=true;
  return count;
}
// Draw the current flight on the panel using the saved layout.
// If there are no flights, the drawing code shows "NO FLIGHTS" instead.
void drawStandalone() {
  Flight flights[kMaxFlights];bool stale;size_t count=visibleFlights(flights,stale);
  // Wrap around to the first flight after the last one.
  if(count) displayed%=count;else displayed=0;
  render(layout,count?&flights[displayed]:nullptr,pixels,modelNow(),stale);
  showPixels(layout.brightness);
}

// ----- USB (serial) communication with Flightdeck Studio -----
// Studio sends one JSON command per line, and the board replies with one
// JSON line. Each command has an "id" so Studio can match up the reply.

// Add one flight's details to a JSON reply (used by the "status" command).
void addFlight(JsonObject row,const Flight& f) {
  row["icao24"]=f.id;row["callsign"]=f.callsign;row["airline_code"]=f.airline;row["airline_name"]=f.airlineName;
  row["latitude"]=f.latitude;row["longitude"]=f.longitude;row["altitude_ft"]=f.altitude;row["speed_knots"]=f.speed;row["heading"]=f.heading;row["vertical_rate_fpm"]=f.verticalRate;
  row["distance_km"]=f.distance;row["on_ground"]=f.ground;row["position_time"]=f.positionTime;
  row["departure_airport"]=f.origin;row["arrival_airport"]=f.destination;row["aircraft_type"]=f.aircraft;
  row["departure_time"]=f.departure;row["arrival_time"]=f.arrival;row["progress_percent"]=f.progress;row["details_source"]=f.sample?"sample":"aeroapi";
}
// Add general board status to a JSON reply: firmware version, Wi-Fi state,
// mode, request counts, free memory, etc. (Never the password or API key.)
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
// Handle one JSON command line received from Studio over USB, and send back
// a JSON reply. Supported commands:
//   hello / status       - report board status (status also lists flights)
//   configure            - change Wi-Fi, API key, mode, poll rate, limit
//   forget_credentials   - erase saved Wi-Fi and API key
//   upload_begin/chunk/commit/abort - receive a layout or preview image
//   resume               - stop showing a preview, go back to flights
//   layout_info/chunk    - send the saved layout back to Studio
void command(char* line) {
  // Read the JSON; ignore the line if it isn't valid or lacks id/cmd.
  StaticJsonDocument<2048> request;
  if(deserializeJson(request,line)) return;
  if(!request["id"].is<uint32_t>() || !request["cmd"].is<const char*>()) return;
  // Start the reply. "status" can list many flights, so it gets more room.
  const char* cmd=request["cmd"];DynamicJsonDocument response(!strcmp(cmd,"status")?20000:2048);
  response["id"]=request["id"];response["ok"]=true;
  // Small helper: mark the reply as failed with an error message.
  auto fail=[&](const char* message){response["ok"]=false;response["error"]=message;};
  if(!strcmp(cmd,"hello") || !strcmp(cmd,"status")) {
    addStatus(response);
    if(!strcmp(cmd,"status")) {Flight flights[kMaxFlights];bool stale;size_t count=visibleFlights(flights,stale);response["stale"]=stale;auto rows=response.createNestedArray("flights");for(size_t i=0;i<count;i++) addFlight(rows.createNestedObject(),flights[i]);}
  } else if(!strcmp(cmd,"configure")) {
    // Start from the current settings and apply only what was sent.
    // Each value is checked; if anything is invalid, nothing is saved.
    Settings next=settings;bool valid=true;
    // Wi-Fi name: up to 32 characters.
    if(request.containsKey("ssid")) {const char* s=request["ssid"];if(!s || strlen(s)>32) valid=false;else copyText(next.ssid,sizeof(next.ssid),s);}
    // Wi-Fi password: empty, or 8-63 characters, or exactly 64 hex digits.
    if(request.containsKey("password")) {const char* s=request["password"];if(!s || strlen(s)>64 || (strlen(s)>0&&strlen(s)<8)) valid=false;else {if(strlen(s)==64) for(const char* p=s;*p;p++) if(!isxdigit(*p)) valid=false;copyText(next.password,sizeof(next.password),s);}}
    // API key: up to 256 normal printable characters (no spaces).
    if(request.containsKey("api_key")) {const char* s=request["api_key"];if(!s || strlen(s)>256) valid=false;else {for(const char* p=s;*p;p++) if(*p<33 || *p>126) valid=false;copyText(next.key,sizeof(next.key),s);}}
    // Mode: "live" (real FlightAware data) or "demo" (fake flights).
    if(request.containsKey("mode")) {const char* s=request["mode"]|"";if(strcmp(s,"live")&&strcmp(s,"demo")) valid=false;else next.live=!strcmp(s,"live");}
    // How often to check for flights: 60 to 3600 seconds.
    if(request.containsKey("poll_seconds")) {if(!request["poll_seconds"].is<unsigned>() || request["poll_seconds"].as<unsigned>()<60 || request["poll_seconds"].as<unsigned>()>3600) valid=false;else next.pollSeconds=request["poll_seconds"];}
    // Max FlightAware requests per hour: 1 to 120.
    if(request.containsKey("max_per_hour")) {if(!request["max_per_hour"].is<unsigned>() || request["max_per_hour"].as<unsigned>()<1 || request["max_per_hour"].as<unsigned>()>120) valid=false;else next.maxPerHour=request["max_per_hour"];}
    // Live mode is only allowed if both Wi-Fi and an API key are set.
    if(next.live && (!next.key[0] || !next.ssid[0])) valid=false;
    if(!valid) fail("Invalid settings. Live mode needs Wi-Fi and an AeroAPI key.");
    else if(!saveSettings(next)) fail("Could not save settings to flash.");
    // Saved OK: start using the new settings and tell the network task.
    else {settings=next;sendNetworkConfig();addStatus(response);}
  } else if(!strcmp(cmd,"forget_credentials")) {
    // Replace the settings with blank defaults (no Wi-Fi, no key, demo mode).
    Settings next; if(!saveSettings(next)) fail("Could not clear saved credentials.");else {settings=next;sendNetworkConfig();addStatus(response);}
  } else if(!strcmp(cmd,"upload_begin")) {
    // Studio is about to send a file in pieces. It's either a "layout" or a
    // "frame" (one preview picture: 1 brightness byte + 2048 pixels x 2 bytes
    // = 4097 bytes). Check the size, then make room in memory for it.
    upload.clear();const char* kind=request["kind"]|"";
    unsigned size=request["length"]|0u;
    if(!request["length"].is<unsigned>() || !request["crc"].is<uint32_t>() || (!strcmp(kind,"layout") ? size<65 || size>kMaxLayoutBytes : strcmp(kind,"frame") || size!=4097)) fail("Invalid upload type or size.");
    else {upload.bytes.reset(new(std::nothrow) uint8_t[size]);if(!upload.bytes) fail("Not enough free memory for upload.");else {upload.size=size;upload.crc=request["crc"];upload.layout=!strcmp(kind,"layout");upload.touched=millis();}}
  } else if(!strcmp(cmd,"upload_chunk")) {
    // One piece of the file, sent as base64 text (up to 384 bytes of data).
    // Pieces must arrive in order; if anything is off, cancel the upload.
    const char* encoded=request["data"]|"";uint8_t bytes[384];size_t got=0;
    if(!upload.bytes || !request["offset"].is<unsigned>() || request["offset"].as<unsigned>()!=upload.received || !strlen(encoded) || strlen(encoded)>512 || mbedtls_base64_decode(bytes,sizeof(bytes),&got,reinterpret_cast<const unsigned char*>(encoded),strlen(encoded)) || !got || got>upload.size-upload.received) {upload.clear();fail("Invalid upload chunk or offset.");}
    else {memcpy(upload.bytes.get()+upload.received,bytes,got);upload.received+=got;upload.touched=millis();}
  } else if(!strcmp(cmd,"upload_commit")) {
    // All pieces sent. Check that we got everything and the fingerprint
    // (CRC) matches what Studio said it should be.
    if(!upload.bytes || upload.received!=upload.size || crc32(upload.bytes.get(),upload.size)!=upload.crc) fail("Incomplete upload or checksum mismatch.");
    else if(upload.layout) {
      // A new layout: check it's valid, save it to flash, then start using it.
      Layout candidate;
      if(!parseLayout(upload.bytes.get(),upload.size,candidate)) fail("Invalid layout.");
      else if(!saveLayoutFile(upload.bytes.get(),upload.size)) fail("Could not save layout; previous layout retained.");
      else {storedLayout=std::move(upload.bytes);layoutData=storedLayout.get();layoutSize=upload.size;layout=candidate;previewActive=false;displayed=0;sendNetworkConfig();drawStandalone();response["layout_crc"]=crc32(layoutData,layoutSize);}
    } else if(!upload.bytes[0]) fail("Invalid brightness.");
    // A preview picture: copy it to the screen and show it briefly.
    else {for(size_t i=0;i<2048;i++) pixels[i]=read16(upload.bytes.get()+1+i*2);showPixels(upload.bytes[0]);previewActive=true;previewAt=millis();}
    upload.clear();
  } else if(!strcmp(cmd,"upload_abort")) upload.clear();
  else if(!strcmp(cmd,"resume")) {previewActive=false;drawStandalone();}
  // Studio can download the saved layout: first ask its size and fingerprint,
  // then ask for it in 384-byte pieces (sent as base64 text).
  else if(!strcmp(cmd,"layout_info")) {response["length"]=layoutSize;response["crc"]=crc32(layoutData,layoutSize);}
  else if(!strcmp(cmd,"layout_chunk")) {
    unsigned offset=request["offset"]|unsigned(layoutSize);
    if(!request["offset"].is<unsigned>() || offset>=layoutSize) fail("Invalid layout offset.");
    else {char b64[513];size_t written;mbedtls_base64_encode(reinterpret_cast<unsigned char*>(b64),sizeof(b64),&written,layoutData+offset,std::min(size_t(384),layoutSize-offset));b64[written]=0;response["data"]=b64;}
  } else fail("Unknown command.");
  // If the reply got too big for its memory, send an error instead.
  if(response.overflowed()) {response.clear();response["id"]=request["id"];fail("Response memory limit exceeded.");}
  // Passwords and API keys are never included in replies or debug output.
  // Send the reply as one line of JSON.
  serializeJson(response,Serial);Serial.write('\n');
}
// Read characters arriving over USB and collect them into lines. Each full
// line is handed to command(). Lines that are too long are thrown away, and
// a half-finished line is dropped after 5 seconds of silence.
// Also cancels an upload if Studio goes quiet for 10 seconds.
void serviceSerial() {
  // "static" = these keep their values between calls.
  static char line[1200];static size_t used=0;static bool discard=false;static uint32_t touched=0;
  if(used && millis()-touched>5000) {used=0;discard=false;}
  // Handle at most 2048 characters per call so the display keeps updating.
  unsigned work=0;
  while(Serial.available() && work++<2048) {
    char c=Serial.read();touched=millis();
    if(c=='\n') {if(!discard && used) {line[used]=0;command(line);}used=0;discard=false;}
    else if(c!='\r' && !discard) {if(used<sizeof(line)-1) line[used++]=c;else {used=0;discard=true;}}
  }
  if(upload.bytes && millis()-upload.touched>10000) upload.clear();
}

// ----- Start-up and main loop -----

// setup() runs once when the board powers on or resets.
void setup() {
  // Start the USB serial connection (115200 = speed).
  Serial.begin(115200);
  bootAt=millis();
  // No wait for Serial: a USB host is optional after provisioning.
  // Open the settings storage and load saved Wi-Fi/API key/etc.
  prefsReady=prefs.begin("flightdeck",false);loadSettings();
  // Start the file system that holds the saved layout.
  storageReady=LittleFS.begin(false);
  // Format only on initial provisioning, never erase a previously mounted
  // filesystem automatically after a flash or power failure.
  if(!storageReady && prefsReady && !prefs.getBool("fsready",false)) {
    storageReady=LittleFS.format() && LittleFS.begin(false);
  }
  if(storageReady && prefsReady) prefs.putBool("fsready",true);
  // Start with the built-in default layout, then replace it with the saved
  // one if there is one (or the backup copy if the main one is damaged).
  parseLayout(kDefaultLayout,sizeof(kDefaultLayout),layout);
  if(storageReady && !loadLayoutFile("/layout.bin")) loadLayoutFile("/layout.bak");
  // Set up the LED panel: which ESP32 pins connect to which panel inputs
  // (from config.h), the panel size, and the panel's driver chip type.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  // Waveshare ESP32-S3 board: uses the E pin and an FM6126A driver chip.
  HUB75_I2S_CFG::i2s_pins pins={PIN_R1,PIN_G1,PIN_B1,PIN_R2,PIN_G2,PIN_B2,PIN_A,PIN_B,PIN_C,PIN_D,PIN_E,PIN_LAT,PIN_OE,PIN_CLK};
  HUB75_I2S_CFG cfg(PANEL_WIDTH,PANEL_HEIGHT,1,pins);
  cfg.clkphase=false;cfg.driver=HUB75_I2S_CFG::FM6126A;
#else
  // Generic ESP32 board: no E pin (-1), default driver.
  HUB75_I2S_CFG::i2s_pins pins={PIN_R1,PIN_G1,PIN_B1,PIN_R2,PIN_G2,PIN_B2,PIN_A,PIN_B,PIN_C,PIN_D,-1,PIN_LAT,PIN_OE,PIN_CLK};
  HUB75_I2S_CFG cfg(PANEL_WIDTH,PANEL_HEIGHT,1,pins);
#endif
  // Use two screen buffers (no flicker) and 6 bits per color to save memory.
  cfg.double_buff=true;cfg.setPixelColorDepthBits(6);
  matrix=new MatrixPanel_I2S_DMA(cfg);panelReady=matrix->begin();
  // Create the two mailboxes (each holds one message) for the network task.
  configQueue=xQueueCreate(1,sizeof(NetworkConfig));stateQueue=xQueueCreate(1,sizeof(NetworkState));
  if(configQueue && stateQueue) {
    // Both supported boards are dual-core. Stack accommodates bounded JSON and
    // the flight snapshots; network data stays off the display task's stack.
    // Start the network task on CPU core 0 (the display runs on the other).
    if(xTaskCreatePinnedToCore(networkTask,"flightaware",24576,nullptr,1,nullptr,0)!=pdPASS) {vQueueDelete(configQueue);vQueueDelete(stateQueue);configQueue=stateQueue=nullptr;}
  }
  // Hand the settings to the network task and draw the first screen.
  sendNetworkConfig();drawStandalone();
}
// loop() runs over and over, forever, after setup() finishes.
void loop() {
  // Handle any commands from Studio over USB.
  serviceSerial();
  // Pick up new flight results from the network task, but only if they match
  // the current settings (same "generation"); otherwise they're outdated.
  if(stateQueue) {static NetworkState incoming;if(xQueueReceive(stateQueue,&incoming,0)==pdTRUE && incoming.generation==generation) networkState=incoming;}
  if(!configQueue) copyText(networkState.message,sizeof(networkState.message),"Network task allocation failed; restart with lower panel depth");
  uint32_t now=millis();
  // A Studio preview picture stays up for 4 seconds, then flights return.
  if(previewActive && now-previewAt>4000) {previewActive=false;lastDraw=0;}
  // Redraw the screen once per second (so times and the progress bar move).
  if(!previewActive && now-lastDraw>=1000) {
    // Every "rotation" seconds (from the layout), move to the next flight.
    if(now-lastRotate>=uint32_t(layout.rotation)*1000) {displayed++;lastRotate=now;}
    drawStandalone();lastDraw=now;
  }
  // Pause 1 millisecond so the board isn't running flat-out for no reason.
  delay(1);
}
