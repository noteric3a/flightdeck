#pragma once

// Generic development-board profiles. A matrix controller may use different pins.
// Set every pin to match the controller schematic before connecting a panel.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
// The Waveshare ESP32-S3-RGB-Matrix panel has green and blue swapped relative
// to the HUB75 library defaults (G=5/15, B=6/16), so they are exchanged here.
#define PIN_R1 4
#define PIN_G1 6
#define PIN_B1 5
#define PIN_R2 7
#define PIN_G2 16
#define PIN_B2 15
#define PIN_A 18
#define PIN_B 8
#define PIN_C 3
#define PIN_D 42
#define PIN_E 9
#define PIN_CLK 41
#define PIN_LAT 40
#define PIN_OE 2
#else
#define PIN_R1 25
#define PIN_G1 26
#define PIN_B1 27
#define PIN_R2 14
#define PIN_G2 12
#define PIN_B2 13
#define PIN_A 23
#define PIN_B 19
#define PIN_C 5
#define PIN_D 17
#define PIN_CLK 16
#define PIN_LAT 4
#define PIN_OE 15
#endif
#define PANEL_WIDTH 64
#define PANEL_HEIGHT 32
#define FRAME_INTERVAL_MS 5000
#define OFFLINE_AFTER_MS 30000
