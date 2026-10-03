#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

MatrixPanel_I2S_DMA *display = nullptr;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("Starting matrix test...");

    HUB75_I2S_CFG mxconfig(
        PANEL_RES_X,
        PANEL_RES_Y,
        PANEL_CHAIN
    );

    mxconfig.gpio.e = 9;
    mxconfig.clkphase = false;
    mxconfig.driver = HUB75_I2S_CFG::FM6126A;

    display = new MatrixPanel_I2S_DMA(mxconfig);

    if (!display->begin()) {
        Serial.println("Matrix initialization FAILED");
        return;
    }

    Serial.println("Matrix initialized!");

    // Keep brightness low because your supply is only 5V / 2A
    display->setBrightness8(25);

    display->clearScreen();
}

void loop() {
    Serial.println("RED");
    display->fillScreen(display->color565(255, 0, 0));
    delay(2000);

    Serial.println("GREEN");
    display->fillScreen(display->color565(0, 255, 0));
    delay(2000);

    Serial.println("BLUE");
    display->fillScreen(display->color565(0, 0, 255));
    delay(2000);

    Serial.println("WHITE");
    display->fillScreen(display->color565(255, 255, 255));
    delay(1000);

    Serial.println("OFF");
    display->clearScreen();
    delay(2000);
}