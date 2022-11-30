#include <Arduino.h>

//Configuration
#include "config.h"

//Helpers
#include "helpers/helper.h"

//LoRaChat
#include "loraChat/loraChatService.h"

//Manager
#include "message/messageManager.h"

//Display
#include "display.h"

//LoRaMesh
#include "loramesh/loraMeshService.h"

//GPS libraries
#include "gps\gpsService.h"

//Bluetooth
#include "bluetooth\bluetoothService.h"

//WiFi
#include "wifi\wifiServerService.h"

#pragma region WiFi

WiFiServerService& wiFiService = WiFiServerService::getInstance();

void initWiFi() {
    wiFiService.initWiFi();
}

#pragma endregion

#pragma region LoRaMesher

LoRaMeshService& loraMeshService = LoRaMeshService::getInstance();

void initLoRaMesher() {
    //Init LoRaMesher
    loraMeshService.initLoraMesherService();
}

#pragma endregion

#pragma region LoRaChat

LoRaChatService& loraChatService = LoRaChatService::getInstance();

void initLoRaChat() {
    //Init LoRaChat
    loraChatService.initLoRaChatService();
}

#pragma endregion

#pragma region GPS

#ifdef GPS_ENABLED
#define UPDATE_GPS_DELAY 10000 //ms

GPSService& gpsService = GPSService::getInstance();

void initGPS() {
    //Initialize GPS
    gpsService.initGPS();
}
#endif

#pragma endregion

#pragma region SerialBT

BluetoothService& bluetoothService = BluetoothService::getInstance();

void initBluetooth() {
    bluetoothService.initBluetooth(String(loraMeshService.getDeviceID()));
}

#pragma endregion

#pragma region Manager

MessageManager& manager = MessageManager::getInstance();

void initManager() {
    manager.init();
    Log.verboseln("Manager initialized");

    manager.addMessageService(&bluetoothService);
    Log.verboseln("Bluetooth service added to manager");

#ifdef GPS_ENABLED
    manager.addMessageService(&gpsService);
    Log.verboseln("GPS service added to manager");
#endif

    manager.addMessageService(&loraMeshService);
    Log.verboseln("LoRaMesher service added to manager");

    manager.addMessageService(&loraChatService);
    Log.verboseln("LoRaChat service added to manager");

    manager.addMessageService(&wiFiService);
    Log.verboseln("WiFi service added to manager");

    Serial.println(manager.getAvailableCommands());
}

#pragma endregion

#pragma region Display

TaskHandle_t display_TaskHandle = NULL;

#define DISPLAY_TASK_DELAY 50 //ms
#define DISPLAY_LINE_TWO_DELAY 10000 //ms
#define DISPLAY_LINE_THREE_DELAY 50000 //ms


void display_Task(void* pvParameters) {

    uint32_t lastLineTwoUpdate = 0;
    uint32_t lastLineThreeUpdate = 0;
#ifdef GPS_ENABLED
    uint32_t lastGPSUpdate = 0;
#endif
    while (true) {
        //Update line two every DISPLAY_LINE_TWO_DELAY ms
        if (millis() - lastLineTwoUpdate > DISPLAY_LINE_TWO_DELAY) {
            lastLineTwoUpdate = millis();
            String lineTwo = String(loraMeshService.getDeviceID()) + " | " + wiFiService.getIP();
            Screen.changeLineTwo(lineTwo);
        }

#ifdef GPS_ENABLED
        //Update line three every DISPLAY_LINE_THREE_DELAY ms
        if (millis() - lastLineThreeUpdate > DISPLAY_LINE_THREE_DELAY) {
            lastLineThreeUpdate = millis();
            String lineThree = gpsService.getGPSString();
            Screen.changeLineThree(lineThree);
        }

        //Update GPS every UPDATE_GPS_DELAY ms
        if (millis() - lastGPSUpdate > UPDATE_GPS_DELAY) {
            lastGPSUpdate = millis();
            gpsService.notifyUpdate();
        }
#endif
        Screen.drawDisplay();
        vTaskDelay(DISPLAY_TASK_DELAY / portTICK_PERIOD_MS);
    }
}

void createUpdateDisplay() {
    int res = xTaskCreate(
        display_Task,
        "Display Task",
        4096,
        (void*) 1,
        2,
        &display_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Display Task creation gave error: %d"), res);
    }
}

void initDisplay() {
    Screen.initDisplay();
    createUpdateDisplay();
}

#pragma endregion

void setup() {
    // Initialize Serial Monitor
    Serial.begin(115200);

    // Initialize Log
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);

#ifdef GPS_ENABLED
    // Initialize GPS
    initGPS();
#endif

    // Initialize LoRaMesh
    initLoRaMesher();

    // Initialize Bluetooth
    initBluetooth();

    // Initialize LoRaChat
    initLoRaChat();

    // Initialize WiFi
    initWiFi();

    // Initialize Manager
    initManager();

    // Initialize Display
    initDisplay();

    // Blink 2 times to show that the device is ready
    Helper::ledBlink(2, 100);
}

void loop() {
    //Suspend this task
    vTaskSuspend(NULL);
}