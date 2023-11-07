#pragma once

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

//OLED pins
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define DISP_ADDRESS 0x3C // Address 0x3D for 128x64

class Display {
public:
    Display();
    void initDisplay();
    void changeLineOne(String str);
    void changeLineTwo(String str);
    void changeLineThree(String str);
    void changeLineFour(String str);
    void changeLineFive(String str);
    void changeLineSix(String str);
    void changeLineSeven(String str);
    // void changeLineFFour(String str);
    void drawDisplay();
    void clearDisplay();
    void printLine(String str, int& x, int y, int size, int minX, bool move);

private:
#if DISPLAY_RST != -1
    Adafruit_SSD1306 display = Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, DISPLAY_RST);
#else
    Adafruit_SSD1306 display = Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT);
#endif
    TaskHandle_t Display_TaskHandle = NULL;

    void changeLine(String str, int pos, int& x, int& minX, int size, bool& move);

    String displayText[6] = {"LoRaTRUST", "", "", "", "", ""};

    String routingText[25];
    int routingSize = 0;
    bool move1, move2 = false;
    bool move3, move4, move5, move6, move7 = false;
    int x1, minX1, x2, minX2, x3, minX3, x4, minX4, x5, minX5, x6, minX6, x7, minX7;
};

extern Display Screen;