#include <Arduino.h>

// Configuration
#include "config.h"

// Log
#include "ArduinoLog.h"

// Helpers
#include "helpers/helper.h"

// Manager
#include "message/messageManager.h"

// Display
#include "display.h"

// LoRaMesh
#include "loramesh/loraMeshService.h"

// Mqtt
#include "mqtt/mqttService.h"

// WiFi
#include "wifi/wifiServerService.h"

// Sensors
#include "sensor/temperature-onewire/temperature.h"

#include "sensor/dht22/dht22.h"

// Led
#include "led/led.h"

#ifdef BLUETOOTH_ENABLED
// Bluetooth
#include "bluetooth/bluetoothService.h"

#endif

// Simulator
#include "simulator/sim.h"

#pragma region Simulator

Sim& simulator = Sim::getInstance();

void initSimulator() {
    // Init Simulator
    simulator.init();
}
#pragma endregion

#pragma region Led

Led& led = Led::getInstance();

void initLed() {
    led.init();
}

#pragma endregion

#pragma region Temperature

Temperature& temperature = Temperature::getInstance();

void initTemperature() {
    temperature.init();
}

#pragma endregion


#pragma region Dht22

Dht22& dht22 = Dht22::getInstance();

void initDht22() {
    dht22.init();
}

#pragma endregion

#pragma region WiFi

WiFiServerService& wiFiService = WiFiServerService::getInstance();

void initWiFi() {
    wiFiService.initWiFi();
}

#pragma endregion

#pragma region LoRaMesher

LoRaMeshService& loraMeshService = LoRaMeshService::getInstance();

void initLoRaMesher() {
    // Init LoRaMesher
    loraMeshService.initLoraMesherService();
}

#pragma endregion

#pragma region Mqtt

MqttService& mqttService = MqttService::getInstance();

void initMqtt() {
    mqttService.initMqtt(String(loraMeshService.getLocalAddress()));
}

#pragma endregion

#pragma region Manager
#ifdef BLUETOOTH_ENABLED
#pragma region Bluetooth

BluetoothService& bluetoothService = BluetoothService::getInstance();

void initBluetooth() {
    bluetoothService.initBluetooth("LoRaMesher");
}

#pragma endregion
#endif

MessageManager& manager = MessageManager::getInstance();

void initManager() {
    manager.init();
    Log.verboseln("Manager initialized");

    manager.addMessageService(&temperature);
    Log.verboseln("Temperature service added to manager");

    manager.addMessageService(&dht22);
    Log.verboseln("dht22 service added to manager");

    manager.addMessageService(&loraMeshService);
    Log.verboseln("LoRaMesher service added to manager");

    manager.addMessageService(&wiFiService);
    Log.verboseln("WiFi service added to manager");

    manager.addMessageService(&mqttService);
    Log.verboseln("Mqtt service added to manager");

    manager.addMessageService(&led);
    Log.verboseln("Led service added to manager");

#ifdef BLUETOOTH_ENABLED
    manager.addMessageService(&bluetoothService);
    Log.verboseln("Bluetooth service added to manager");
#endif

    manager.addMessageService(&simulator);
    Log.verboseln("Simulator service added to manager");

    Serial.println(manager.getAvailableCommands());
}

#pragma endregion

#pragma region Display

TaskHandle_t display_TaskHandle = NULL;

#define DISPLAY_TASK_DELAY 50          // ms
#define DISPLAY_LINE_TWO_DELAY 10000   // ms
#define DISPLAY_LINE_THREE_DELAY 50000 // ms

void display_Task(void* pvParameters) {

    uint32_t lastLineTwoUpdate = 0;
    uint32_t lastLineThreeUpdate = 0;
    char lineThree[25];
    while (true) {
        // Update line two every DISPLAY_LINE_TWO_DELAY ms
        if (millis() - lastLineTwoUpdate > DISPLAY_LINE_TWO_DELAY) {
            lastLineTwoUpdate = millis();
            String lineTwo = String(loraMeshService.getLocalAddress()) + " | " + wiFiService.getIP();
            // write availbale amount of ram to lineThree
            sprintf(lineThree, "Free ram: %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            Screen.changeLineTwo(lineTwo);
            Screen.changeLineThree(lineThree);
        }

        Screen.drawDisplay();
        vTaskDelay(DISPLAY_TASK_DELAY / portTICK_PERIOD_MS);
    }
}

void createUpdateDisplay() {
    int res = xTaskCreate(
        display_Task,
        "Display Task",
        4096,
        (void*)1,
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

    // Initialize Display
    initDisplay();

    Log.infoln(F("Free ram before starting Manager %d"), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Initialize Manager
    initManager();

    Log.infoln(F("Free ram before starting LoRaMesher %d"), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    // Initialize LoRaMesh
    initLoRaMesher();

    Log.infoln(F("Free ram before starting WiFi %d"), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Initialize WiFi
    initWiFi();

#ifdef BLUETOOTH_ENABLED

    Log.infoln(F("Free ram before starting BLE %d"), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    initBluetooth();

#endif

    Log.infoln(F("Free ram before starting mqtt %d"), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Initialize Mqtt
    initMqtt();

    Log.infoln(F("Free ram before starting Display %d"), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Initialize Temperature
    //initTemperature();

    // Initialize Dht22
    initDht22();

    // Initialize Led
    initLed();

    // Initialize Simulator
    initSimulator();

    // Blink 2 times to show that the device is ready
    Helper::ledBlink(2, 100);
}

void loop() {
    // Suspend this task
    vTaskSuspend(NULL);
}