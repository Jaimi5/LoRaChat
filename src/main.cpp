#include <Arduino.h>

//Configuration
#include "config.h"

//Helpers
#include "helpers/helper.h"

//Manager
#include "message/messageManager.h"

//Display
#include "display.h"

//LoRaMesh
#include "loramesh/loraMeshService.h"

//Helpers
// #include "contacts.h"

//GPS libraries
#include "gps\gpsService.h"

//Bluetooth
#include "bluetooth\bluetoothService.h"

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

void initializeGPS() {
    //Initialize GPS
    gpsService.initGPS();

    //Initialize GPS Display
    createUpdateGPSDisplay();
}

#pragma endregion

#pragma region SerialBT

BluetoothService& bluetoothService = BluetoothService::getInstance();

void initBluetooth() {
    bluetoothService.initBluetooth("a1");
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

    // initializeLoraMesher();

    // Initialize GPS
    initializeGPS();

    // Initialize Bluetooth
    initBluetooth();

    // Initialize Manager
    initManager();

    // Blink 2 times to show that the device is ready
    Helper::ledBlink(2, 100);
}

void loop() {
    Screen.drawDisplay();
    vTaskDelay(50 / portTICK_PERIOD_MS);
}