#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <math.h>
#include <ctype.h>
#include <string.h>

// ============================================================
// FlightDeck Offline Hardware Demo
// 64x32 HUB75 matrix
// Waveshare ESP32-S3 RGB Matrix controller
//
// Demo data mirrors backend/flights.py
// Layout mirrors dist/defaults.json
// ============================================================

#define PANEL_WIDTH  64
#define PANEL_HEIGHT 32
#define PANEL_CHAIN  1

MatrixPanel_I2S_DMA *display = nullptr;

// Repo default rotation time
constexpr unsigned long ROTATION_MS = 15000;

// Set true to show UAL700 as well.
// Your normal repo default hides ground aircraft.
constexpr bool INCLUDE_GROUND_DEMO = true;


// ============================================================
// DEMO FLIGHTS
// From backend/flights.py
// ============================================================

struct DemoFlight {
    const char *callsign;
    const char *airline;

    const char *departure;
    const char *arrival;

    const char *aircraft;

    int departedMinutesAgo;
    int arrivalMinutesFromStart;

    bool onGround;
};


DemoFlight flights[] = {

    {
        "UAL247",
        "UAL",
        "PHL",
        "ORD",
        "B787-9",
        510,
        225,
        false
    },

    {
        "DAL1082",
        "DAL",
        "ATL",
        "JFK",
        "E175",
        75,
        45,
        false
    },

    {
        "SWA516",
        "SWA",
        "MDW",
        "DEN",
        "B737-800",
        35,
        100,
        false
    },

    {
        "AAL903",
        "AAL",
        "DFW",
        "LAX",
        "A321",
        130,
        25,
        false
    },

    {
        "N625EC",
        "PVT",
        "---",
        "---",
        "C172",
        -1,
        -1,
        false
    },

    {
        "UAL700",
        "UAL",
        "ORD",
        "SFO",
        "B737-800",
        -1,
        -1,
        true
    }
};


constexpr int FLIGHT_COUNT =
    sizeof(flights) / sizeof(flights[0]);


// ============================================================
// COLORS
// ============================================================

uint16_t BLACK;
uint16_t WHITE;
uint16_t BLUE;
uint16_t GREEN;
uint16_t GRAY;


// Generated from dist/defaults.json; shared with Studio and autonomous firmware.
#include "offline_logo_data.h"


// ============================================================
// 3x5 FONT
//
// Matches FlightDeck's dist/font.json.
// ============================================================

void glyph(char c, uint8_t rows[5]) {

    for (int i = 0; i < 5; i++) {
        rows[i] = 0;
    }

    switch (c) {

        // -------------------
        // Numbers
        // -------------------

        case '0': {
            uint8_t r[] = {7,5,5,5,7};
            memcpy(rows,r,5);
            break;
        }

        case '1': {
            uint8_t r[] = {2,6,2,2,7};
            memcpy(rows,r,5);
            break;
        }

        case '2': {
            uint8_t r[] = {6,1,2,4,7};
            memcpy(rows,r,5);
            break;
        }

        case '3': {
            uint8_t r[] = {6,1,2,1,6};
            memcpy(rows,r,5);
            break;
        }

        case '4': {
            uint8_t r[] = {5,5,7,1,1};
            memcpy(rows,r,5);
            break;
        }

        case '5': {
            uint8_t r[] = {7,4,6,1,6};
            memcpy(rows,r,5);
            break;
        }

        case '6': {
            uint8_t r[] = {3,4,7,5,7};
            memcpy(rows,r,5);
            break;
        }

        case '7': {
            uint8_t r[] = {7,1,2,2,2};
            memcpy(rows,r,5);
            break;
        }

        case '8': {
            uint8_t r[] = {7,5,7,5,7};
            memcpy(rows,r,5);
            break;
        }

        case '9': {
            uint8_t r[] = {7,5,7,1,6};
            memcpy(rows,r,5);
            break;
        }


        // -------------------
        // Alphabet
        // -------------------

        case 'A': {
            uint8_t r[] = {2,5,7,5,5};
            memcpy(rows,r,5);
            break;
        }

        case 'B': {
            uint8_t r[] = {6,5,6,5,6};
            memcpy(rows,r,5);
            break;
        }

        case 'C': {
            uint8_t r[] = {3,4,4,4,3};
            memcpy(rows,r,5);
            break;
        }

        case 'D': {
            uint8_t r[] = {6,5,5,5,6};
            memcpy(rows,r,5);
            break;
        }

        case 'E': {
            uint8_t r[] = {7,4,6,4,7};
            memcpy(rows,r,5);
            break;
        }

        case 'F': {
            uint8_t r[] = {7,4,6,4,4};
            memcpy(rows,r,5);
            break;
        }

        case 'G': {
            uint8_t r[] = {3,4,5,5,3};
            memcpy(rows,r,5);
            break;
        }

        case 'H': {
            uint8_t r[] = {5,5,7,5,5};
            memcpy(rows,r,5);
            break;
        }

        case 'I': {
            uint8_t r[] = {7,2,2,2,7};
            memcpy(rows,r,5);
            break;
        }

        case 'J': {
            uint8_t r[] = {1,1,1,5,2};
            memcpy(rows,r,5);
            break;
        }

        case 'K': {
            uint8_t r[] = {5,5,6,5,5};
            memcpy(rows,r,5);
            break;
        }

        case 'L': {
            uint8_t r[] = {4,4,4,4,7};
            memcpy(rows,r,5);
            break;
        }

        case 'M': {
            uint8_t r[] = {5,7,7,5,5};
            memcpy(rows,r,5);
            break;
        }

        case 'N': {
            uint8_t r[] = {5,7,7,7,5};
            memcpy(rows,r,5);
            break;
        }

        case 'O': {
            uint8_t r[] = {2,5,5,5,2};
            memcpy(rows,r,5);
            break;
        }

        case 'P': {
            uint8_t r[] = {6,5,6,4,4};
            memcpy(rows,r,5);
            break;
        }

        case 'Q': {
            uint8_t r[] = {2,5,5,7,3};
            memcpy(rows,r,5);
            break;
        }

        case 'R': {
            uint8_t r[] = {6,5,6,5,5};
            memcpy(rows,r,5);
            break;
        }

        case 'S': {
            uint8_t r[] = {3,4,2,1,6};
            memcpy(rows,r,5);
            break;
        }

        case 'T': {
            uint8_t r[] = {7,2,2,2,2};
            memcpy(rows,r,5);
            break;
        }

        case 'U': {
            uint8_t r[] = {5,5,5,5,7};
            memcpy(rows,r,5);
            break;
        }

        case 'V': {
            uint8_t r[] = {5,5,5,5,2};
            memcpy(rows,r,5);
            break;
        }

        case 'W': {
            uint8_t r[] = {5,5,7,7,5};
            memcpy(rows,r,5);
            break;
        }

        case 'X': {
            uint8_t r[] = {5,5,2,5,5};
            memcpy(rows,r,5);
            break;
        }

        case 'Y': {
            uint8_t r[] = {5,5,2,2,2};
            memcpy(rows,r,5);
            break;
        }

        case 'Z': {
            uint8_t r[] = {7,1,2,4,7};
            memcpy(rows,r,5);
            break;
        }


        // -------------------
        // Symbols
        // -------------------

        case '-': {
            uint8_t r[] = {0,0,7,0,0};
            memcpy(rows,r,5);
            break;
        }

        case '>': {
            uint8_t r[] = {4,2,1,2,4};
            memcpy(rows,r,5);
            break;
        }

        case ':': {
            uint8_t r[] = {0,2,0,2,0};
            memcpy(rows,r,5);
            break;
        }

        case '.': {
            uint8_t r[] = {0,0,0,0,2};
            memcpy(rows,r,5);
            break;
        }

        case '/': {
            uint8_t r[] = {1,1,2,4,4};
            memcpy(rows,r,5);
            break;
        }

        case '+': {
            uint8_t r[] = {0,2,7,2,0};
            memcpy(rows,r,5);
            break;
        }

        case '?': {
            uint8_t r[] = {6,1,2,0,2};
            memcpy(rows,r,5);
            break;
        }

        case ' ':
        default:
            break;
    }
}


// ============================================================
// DRAW SMALL TEXT
// ============================================================

void drawTinyChar(
    int x,
    int y,
    char c,
    uint16_t color
) {

    uint8_t rows[5];

    glyph(
        toupper((unsigned char)c),
        rows
    );

    for (int yy = 0; yy < 5; yy++) {

        for (int xx = 0; xx < 3; xx++) {

            if (
                rows[yy] &
                (1 << (2 - xx))
            ) {

                display->drawPixel(
                    x + xx,
                    y + yy,
                    color
                );
            }
        }
    }
}


void drawTinyText(
    int x,
    int y,
    const char *text,
    uint16_t color,
    int maxWidth
) {

    int startX = x;

    while (*text) {

        if (
            x + 3 >
            startX + maxWidth
        ) {
            break;
        }

        drawTinyChar(
            x,
            y,
            *text,
            color
        );

        x += 4;
        text++;
    }
}


// ============================================================
// DRAW AIRLINE LOGO
//
// Source = 28x28
// Display = 20x20
//
// Same nearest-neighbor behavior used by FlightDeck renderer.
// ============================================================

void drawLogo(
    const char *airline
) {

    const uint64_t *bitmap = nullptr;


    if (!strcmp(airline, "UAL")) {

        bitmap = LOGO_UAL;
    }

    else if (!strcmp(airline, "DAL")) {

        bitmap = LOGO_DAL;
    }

    else if (!strcmp(airline, "AAL")) {

        bitmap = LOGO_AAL;
    }

    else if (!strcmp(airline, "SWA")) {

        bitmap = LOGO_SWA;
    }


    // --------------------------------------------------------
    // Unknown airline fallback
    // --------------------------------------------------------

    if (bitmap == nullptr) {

        display->fillRect(
            2,
            2,
            20,
            20,
            BLACK
        );

        drawTinyText(
            6,
            10,
            airline,
            BLUE,
            12
        );

        return;
    }


    constexpr int SRC_SIZE = 28;

    constexpr int DEST_X = 2;
    constexpr int DEST_Y = 2;

    constexpr int DEST_W = 20;
    constexpr int DEST_H = 20;


    for (int y = 0; y < DEST_H; y++) {

        int srcY =
            y * SRC_SIZE / DEST_H;


        uint64_t row =
            bitmap[srcY];


        for (int x = 0; x < DEST_W; x++) {

            int srcX =
                x * SRC_SIZE / DEST_W;


            uint8_t paletteIndex =
                (row >> (srcX * 2))
                & 0x03;


            uint16_t color =
                logoColor(
                    airline,
                    paletteIndex
                );


            display->drawPixel(
                DEST_X + x,
                DEST_Y + y,
                color
            );
        }
    }
}


// ============================================================
// AIRPLANE
//
// Same 7x7 pattern as backend/journey.py
// ============================================================

const char *PLANE[7] = {

    "0010000",
    "0001000",
    "1001100",
    "1111111",
    "1001100",
    "0001000",
    "0010000"

};


void drawPlane(
    int x,
    int y,
    uint16_t color
) {

    for (int yy = 0; yy < 7; yy++) {

        for (int xx = 0; xx < 7; xx++) {

            if (
                PLANE[yy][xx] == '1'
            ) {

                display->drawPixel(
                    x + xx,
                    y + yy,
                    color
                );
            }
        }
    }
}


// ============================================================
// REMAINING TIME
// ============================================================

void getEtaText(
    const DemoFlight &flight,
    char *buffer,
    size_t length
) {

    if (
        flight.departedMinutesAgo < 0 ||
        flight.arrivalMinutesFromStart < 0
    ) {

        snprintf(
            buffer,
            length,
            "--"
        );

        return;
    }


    unsigned long elapsedSeconds =
        millis() / 1000;


    long remainingSeconds =
        flight.arrivalMinutesFromStart * 60L
        - elapsedSeconds;


    if (remainingSeconds < 0) {

        remainingSeconds = 0;
    }


    int minutes =
        (remainingSeconds + 59) / 60;


    if (minutes >= 6000) {

        snprintf(
            buffer,
            length,
            ">99H"
        );
    }

    else if (minutes >= 60) {

        snprintf(
            buffer,
            length,
            "%dH%02dM",
            minutes / 60,
            minutes % 60
        );
    }

    else {

        snprintf(
            buffer,
            length,
            "%dM",
            minutes
        );
    }
}


// ============================================================
// FLIGHT PROGRESS
// ============================================================

float getProgress(
    const DemoFlight &flight
) {

    if (
        flight.departedMinutesAgo < 0 ||
        flight.arrivalMinutesFromStart < 0
    ) {

        return -1.0;
    }


    double elapsedSinceBootMinutes =
        millis() / 60000.0;


    double elapsedFlightMinutes =
        flight.departedMinutesAgo
        + elapsedSinceBootMinutes;


    double totalFlightMinutes =
        flight.departedMinutesAgo
        + flight.arrivalMinutesFromStart;


    float progress =
        elapsedFlightMinutes
        / totalFlightMinutes;


    if (progress < 0.0) {
        progress = 0.0;
    }

    if (progress > 1.0) {
        progress = 1.0;
    }


    return progress;
}


// ============================================================
// DRAW PROGRESS BAR
//
// Matches:
//
// x = 27
// y = 23
// width = 34
// height = 7
// ============================================================

void drawProgress(
    float progress
) {

    constexpr int x = 27;
    constexpr int y = 23;

    constexpr int width = 34;
    constexpr int height = 7;


    int centerY =
        y + height / 2;


    // --------------------------------------------------------
    // No journey information
    // --------------------------------------------------------

    if (progress < 0) {

        drawTinyText(
            x,
            centerY - 2,
            "--",
            GRAY,
            width
        );

        return;
    }


    int planeX =
        x +
        round(
            progress * (width - 7)
        );


    int divider =
        planeX + 3;


    // Green = completed
    // White = remaining
    for (
        int xx = x;
        xx < x + width;
        xx++
    ) {

        display->drawPixel(
            xx,
            centerY,
            xx < divider
                ? GREEN
                : WHITE
        );
    }


    drawPlane(
        planeX,
        centerY - 3,
        kDemoPlaneColor
    );
}


// ============================================================
// DRAW COMPLETE FLIGHTDECK SCREEN
// ============================================================

void drawFlight(
    const DemoFlight &flight
) {

    display->fillScreen(
        BLACK
    );


    // ========================================================
    // LOGO
    //
    // x = 2
    // y = 2
    // 20x20
    // ========================================================

    drawLogo(
        flight.airline
    );


    // ========================================================
    // CALLSIGN
    //
    // x = 27
    // y = 2
    // ========================================================

    drawTinyText(
        27,
        2,
        flight.callsign,
        WHITE,
        37
    );


    // ========================================================
    // ROUTE
    // ========================================================

    char route[16];

    snprintf(
        route,
        sizeof(route),
        "%s->%s",
        flight.departure,
        flight.arrival
    );


    drawTinyText(
        27,
        10,
        route,
        WHITE,
        37
    );


    // ========================================================
    // AIRCRAFT TYPE
    // ========================================================

    drawTinyText(
        27,
        17,
        flight.aircraft,
        kDemoAircraftColor,
        37
    );


    // ========================================================
    // TIME REMAINING
    // ========================================================

    char eta[10];

    getEtaText(
        flight,
        eta,
        sizeof(eta)
    );


    drawTinyText(
        2,
        24,
        eta,
        WHITE,
        23
    );


    // ========================================================
    // PROGRESS
    // ========================================================

    drawProgress(
        getProgress(
            flight
        )
    );
}


// ============================================================
// GET NEXT DEMO FLIGHT
// ============================================================

int nextFlight(
    int current
) {

    for (
        int count = 0;
        count < FLIGHT_COUNT;
        count++
    ) {

        current =
            (current + 1)
            % FLIGHT_COUNT;


        if (
            INCLUDE_GROUND_DEMO ||
            !flights[current].onGround
        ) {

            return current;
        }
    }


    return 0;
}


// ============================================================
// SERIAL DEBUG
// ============================================================

void printFlightInfo(
    const DemoFlight &flight
) {

    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.print(
        "Flight: "
    );

    Serial.println(
        flight.callsign
    );


    Serial.print(
        "Airline: "
    );

    Serial.println(
        flight.airline
    );


    Serial.print(
        "Route: "
    );

    Serial.print(
        flight.departure
    );

    Serial.print(
        " -> "
    );

    Serial.println(
        flight.arrival
    );


    Serial.print(
        "Aircraft: "
    );

    Serial.println(
        flight.aircraft
    );


    char eta[10];

    getEtaText(
        flight,
        eta,
        sizeof(eta)
    );


    Serial.print(
        "Remaining: "
    );

    Serial.println(
        eta
    );


    Serial.println(
        "================================"
    );
}


// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(
        115200
    );


    delay(
        1000
    );


    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "     FlightDeck Demo Mode"
    );

    Serial.println(
        "================================"
    );


    // ========================================================
    // Matrix configuration
    // ========================================================

    HUB75_I2S_CFG config(
        PANEL_WIDTH,
        PANEL_HEIGHT,
        PANEL_CHAIN
    );


    // Waveshare ESP32-S3 RGB Matrix settings
    config.gpio.e = 9;

    config.clkphase = false;

    config.driver =
        HUB75_I2S_CFG::FM6126A;


    display =
        new MatrixPanel_I2S_DMA(
            config
        );


    if (!display->begin()) {

        Serial.println(
            "ERROR: Matrix initialization failed!"
        );


        while (true) {

            delay(
                1000
            );
        }
    }


    // ========================================================
    // IMPORTANT:
    // Keep this low with your current 5V / 2A supply.
    //
    // 0 - 255
    // ========================================================

    display->setBrightness8(
        25
    );


    // ========================================================
    // Define colors
    // ========================================================

    BLACK =
        display->color565(
            0,
            0,
            0
        );


    WHITE =
        display->color565(
            255,
            255,
            255
        );


    BLUE =
        display->color565(
            0,
            0,
            255
        );


    GREEN =
        display->color565(
            0,
            255,
            0
        );


    GRAY =
        display->color565(
            0x7A,
            0x99,
            0xAD
        );


    display->fillScreen(
        BLACK
    );


    Serial.println(
        "Matrix initialized."
    );


    Serial.println(
        "Cycling demo flights every 15 seconds."
    );


    Serial.println(
        "Pixel airline logos enabled."
    );


    // Draw first flight immediately
    drawFlight(
        flights[0]
    );


    printFlightInfo(
        flights[0]
    );
}


// ============================================================
// LOOP
// ============================================================

void loop() {

    static int flightIndex = 0;


    static unsigned long lastRotation =
        millis();


    static unsigned long lastRefresh =
        millis();


    unsigned long now =
        millis();


    // ========================================================
    // Refresh once per second
    //
    // Allows ETA and progress to update.
    // ========================================================

    if (
        now - lastRefresh
        >= 1000
    ) {

        lastRefresh =
            now;


        drawFlight(
            flights[flightIndex]
        );
    }


    // ========================================================
    // Rotate flights
    // ========================================================

    if (
        now - lastRotation
        >= ROTATION_MS
    ) {

        lastRotation =
            now;


        flightIndex =
            nextFlight(
                flightIndex
            );


        drawFlight(
            flights[flightIndex]
        );


        printFlightInfo(
            flights[flightIndex]
        );
    }
}