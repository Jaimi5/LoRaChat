#pragma once

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//OLED pins
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define DISP_ADDRESS 0x3C // Address 0x3D for 128x64

#ifndef OLED_SDA
#define OLED_SDA SDA
#endif
#ifndef OLED_SCL
#define OLED_SCL SCL
#endif

#ifndef OLED_RST
#define OLED_RST -1
#endif

class Display {
public:
    Display();
    void initDisplay();
    void changeLineOne(String str);
    void changeLineTwo(String str);
    void changeLineThree(String str);
    void changeLineFour();
    void changeLineFFour(String str);
    void drawDisplay();
    void clearDisplay();
    void printLine(String str, int& x, int y, int size, int minX, bool move);

private:
    Adafruit_SSD1306 display = Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT);
    TaskHandle_t Display_TaskHandle = NULL;

    void changeLine(String str, int pos, int& x, int& minX, int size, bool& move);

    String displayText[4] = {"LoRaChat v0.2", "", "", ""};

    String routingText[25];
    int routingSize = 0;
    bool move1, move2 = false;
    bool move3, move4, move5, move6 = true;
    int x1, minX1, x2, minX2, x3, minX3, x4, minX4, x5, minX5, x6, minX6;
};

extern Display Screen;