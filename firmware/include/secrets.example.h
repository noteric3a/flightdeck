#pragma once
// Copy to secrets.h, which is ignored by Git.
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define SERVICE_URL "http://192.168.1.100:8000"
#define DEVICE_TOKEN "COPY_DEVICE_TOKEN_FROM_ENV"
#define DEVICE_ID "flightdeck-1"
// For HTTPS paste the issuing CA certificate in PEM format below.
// The firmware verifies TLS; it never calls setInsecure().
static const char ROOT_CA[] = R"PEM()PEM";
