#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <time.h>
#include "config.h"
#include "frame_protocol.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
// Compiles for CI; cannot connect until secrets.h is configured.
#include "secrets.example.h"
#endif

MatrixPanel_I2S_DMA* matrix = nullptr;
uint8_t packet[flightdeck::kPacketBytes];
uint32_t acknowledgedRevision = 0;
uint32_t lastGoodFrame = 0;
uint32_t lastAttempt = 0;
uint32_t lastWifiAttempt = 0;
uint32_t pollInterval = FRAME_INTERVAL_MS;
bool offlineShown = false;

void showMessage(const char* a, const char* b) {
  matrix->fillScreen(0);
  matrix->setTextWrap(false);
  matrix->setTextSize(1);
  matrix->setTextColor(matrix->color565(110, 165, 195));
  matrix->setCursor(2, 6);
  matrix->print(a);
  matrix->setCursor(2, 17);
  matrix->print(b);
  matrix->flipDMABuffer();
}

bool readFrame(HTTPClient& http) {
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("Frame request failed: HTTP %d\n", code);
    http.end();
    return false;
  }
  // Uvicorn and the documented deployment send Content-Length. Fail closed on
  // unknown/chunked lengths instead of displaying partially decoded HTTP data.
  if (http.getSize() != static_cast<int>(flightdeck::kPacketBytes)) {
    Serial.println("Unexpected frame length");
    http.end();
    return false;
  }
  auto* stream = http.getStreamPtr();
  size_t received = 0;
  const uint32_t start = millis();
  while (received < sizeof(packet) && millis() - start < 10000) {
    const int available = stream->available();
    if (available > 0) {
      const size_t count = min(static_cast<size_t>(available), sizeof(packet) - received);
      const int got = stream->read(packet + received, count);
      if (got > 0) received += static_cast<size_t>(got);
    } else if (!http.connected()) break;
    else delay(1);
  }
  http.end();
  if (!flightdeck::validFrame(packet, received)) {
    Serial.println("Rejected incomplete or corrupt frame");
    return false;
  }
  matrix->setBrightness8(packet[8]);
  for (int y = 0; y < PANEL_HEIGHT; ++y)
    for (int x = 0; x < PANEL_WIDTH; ++x)
      matrix->drawPixel(x, y, flightdeck::read16(packet + flightdeck::kHeaderBytes + (y * PANEL_WIDTH + x) * 2));
  matrix->flipDMABuffer();
  acknowledgedRevision = flightdeck::read32(packet + 10);
  Serial.printf("Displayed layout %lu, flags %u\n", static_cast<unsigned long>(acknowledgedRevision), packet[9]);
  return true;
}

bool fetchFrame() {
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  String base(SERVICE_URL);
  const String url = base + "/api/device/frame";
  bool started = false;
  if (base.startsWith("https://")) {
    if (strlen(ROOT_CA) < 100 || time(nullptr) < 1700000000) {
      Serial.println("HTTPS needs a CA certificate and synchronized clock");
      return false;
    }
    secure.setCACert(ROOT_CA);
    started = http.begin(secure, url);
  } else if (base.startsWith("http://")) {
    started = http.begin(plain, url);
  }
  if (!started) return false;
  http.setConnectTimeout(5000);
  http.setTimeout(25000);
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.addHeader("Authorization", String("Bearer ") + DEVICE_TOKEN);
  http.addHeader("X-Device-ID", DEVICE_ID);
  http.addHeader("X-Frame-Ack", String(acknowledgedRevision));
  return readFrame(http);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("FlightDeck starting...");
  HUB75_I2S_CFG::i2s_pins pins = {PIN_R1, PIN_G1, PIN_B1, PIN_R2, PIN_G2, PIN_B2, PIN_A, PIN_B, PIN_C, PIN_D, -1, PIN_LAT, PIN_OE, PIN_CLK};
  HUB75_I2S_CFG cfg(PANEL_WIDTH, PANEL_HEIGHT, 1, pins);
  cfg.double_buff = true;
  matrix = new MatrixPanel_I2S_DMA(cfg);
  if (!matrix->begin()) {
    Serial.println("Panel initialization failed");
    while (true) delay(1000);
  }
  matrix->setBrightness8(40);
  showMessage("FLIGHT", "DECK");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

void loop() {
  const uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiAttempt > 10000) {
      lastWifiAttempt = now;
      WiFi.reconnect();
    }
  } else if (now - lastAttempt >= pollInterval) {
    lastAttempt = now;
    if (fetchFrame()) {
      lastGoodFrame = millis();
      offlineShown = false;
      pollInterval = FRAME_INTERVAL_MS;
    } else {
      pollInterval = min(pollInterval * 2, uint32_t(30000));
    }
  }
  if (!offlineShown && millis() - lastGoodFrame > OFFLINE_AFTER_MS) {
    showMessage("SERVICE", "OFFLINE");
    offlineShown = true;
  }
  delay(10);
}
