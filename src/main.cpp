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

GPSService& gpsService = GPSService::getInstance();

TaskHandle_t gpsDisplay_TaskHandle = NULL;

void gpsDisplay_Task(void* pvParameters) {
    while (true) {
        String gpsString = gpsService.getGPSUpdatedWait();

        Screen.changeLineTwo(gpsString);
        vTaskDelay(50000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Create a Receive Messages Task and add it to the LoRaMesher
 *
 */
void createUpdateGPSDisplay() {
    int res = xTaskCreate(
        gpsDisplay_Task,
        "Gps Display Task",
        4096,
        (void*) 1,
        2,
        &gpsDisplay_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Gps Display Task creation gave error: %d"), res);
    }
}

void initGPS() {
    //Initialize GPS
    gpsService.initGPS();

    //Initialize GPS Display
    createUpdateGPSDisplay();
}

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

    manager.addMessageService(&gpsService);
    Log.verboseln("GPS service added to manager");

    manager.addMessageService(&loraMeshService);
    Log.verboseln("LoRaMesher service added to manager");

    manager.addMessageService(&loraChatService);
    Log.verboseln("LoRaChat service added to manager");

    manager.addMessageService(&wiFiService);
    Log.verboseln("WiFi service added to manager");

    Serial.println(manager.getAvailableCommands());
}

#pragma endregion

void setup() {
    // Initialize Serial Monitor
    Serial.begin(115200);

    // Initialize Log
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);

    // Initialize Screen
    Screen.initDisplay();

    // Initialize GPS
    initGPS();

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

    // Blink 2 times to show that the device is ready
    Helper::ledBlink(2, 100);
}

void loop() {
    Screen.drawDisplay();
    vTaskDelay(50 / portTICK_PERIOD_MS);
}