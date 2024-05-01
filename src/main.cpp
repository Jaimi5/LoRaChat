#include <Arduino.h>

//Configuration
#include "config.h"

//Log
#include "esp_log.h"
#include "esp32-hal-log.h"

//Manager
#include "message/messageManager.h"

//LoRaMesh
#include "loramesh/loraMeshService.h"

//WiFi
#include "wifi/wifiServerService.h"

//Sensors
#include "sensor/sensorService.h"

//Metadata
#include "sensor/metadata/metadata.h"

// Routing Table
#include "routing-table/rtService.h"


static const char* TAG = "Main";


// Routing Table
#pragma region Routing Table
RtService& rtService = RtService::getInstance();

void initRT() {
    rtService.init();
}

#pragma endregion

// Battery
#pragma region Battery

#include "battery/battery.h"

Battery& battery = Battery::getInstance();

void initBattery() {
    battery.init();
}

#pragma endregion

// Simulator
#pragma region Simulator

#include "simulator/sim.h"

Sim& simulator = Sim::getInstance();

void initSimulator() {
    // Init Simulator
    simulator.init();
}
#pragma endregion

#pragma region Led
#include "led/led.h"

Led& led = Led::getInstance();

void initLed() {
    led.init();
}

#pragma endregion

#pragma region Metadata

Metadata& metadata = Metadata::getInstance();

void initMetadata() {
    metadata.initMetadata();
}

#pragma endregion

#pragma region Sensors

SensorService& sensorService = SensorService::getInstance();

void initSensors() {
    sensorService.init();
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
    //Init LoRaMesher
    loraMeshService.initLoraMesherService();
}

#pragma endregion


#pragma region MQTT
#include "mqtt/mqttService.h"

MqttService& mqttService = MqttService::getInstance();

void initMQTT() {
    mqttService.initMqtt(String(loraMeshService.getLocalAddress()));
}

#pragma endregion


#pragma region GPS

#include "gps/gpsService.h"

GPSService& gpsService = GPSService::getInstance();

void initGPS() {
    //Initialize GPS
    gpsService.initGPS();
}

#pragma endregion

#ifdef BLUETOOTH_ENABLED
#pragma region SerialBT
#include "bluetooth/bluetoothService.h"
BluetoothService& bluetoothService = BluetoothService::getInstance();

void initBluetooth() {
    bluetoothService.initBluetooth(String(loraMeshService.getLocalAddress(), HEX));
}

#pragma endregion
#endif

#pragma region Manager

MessageManager& manager = MessageManager::getInstance();

void initManager() {
    manager.init();
    ESP_LOGV(TAG, "Manager initialized");

#ifdef BLUETOOTH_ENABLED
    manager.addMessageService(&bluetoothService);
    ESP_LOGV(TAG, "Bluetooth service added to manager");
#endif

    manager.addMessageService(&gpsService);
    ESP_LOGV(TAG, "GPS service added to manager");

    manager.addMessageService(&loraMeshService);
    ESP_LOGV(TAG, "LoRaMesher service added to manager");

    manager.addMessageService(&wiFiService);
    ESP_LOGV(TAG, "WiFi service added to manager");

    manager.addMessageService(&mqttService);
    ESP_LOGV(TAG, "MQTT service added to manager");

    manager.addMessageService(&led);
    ESP_LOGV(TAG, "Led service added to manager");

    manager.addMessageService(&metadata);
    ESP_LOGV(TAG, "Metadata service added to manager");

    manager.addMessageService(&sensorService);
    ESP_LOGV(TAG, "Sensors service added to manager");

    manager.addMessageService(&simulator);
    ESP_LOGV(TAG, "Simulator service added to manager");

    manager.addMessageService(&rtService);
    ESP_LOGV(TAG, "Routing Table service added to manager");

    Serial.println(manager.getAvailableCommands());
}

#pragma endregion

// #pragma region AXP20x
// #include <axp20x.h>

// AXP20X_Class axp;

// void initAXP() {
//     Wire.begin(21, 22);
//     if (axp.begin(Wire, AXP192_SLAVE_ADDRESS) == AXP_FAIL) {
//         Serial.println(F("failed to initialize communication with AXP192"));
//     }
//     Serial.println(axp.getBattVoltage());
// }

// float getBatteryVoltage() {
//     return axp.getBattVoltage();
// }

// #pragma endregion

#ifdef DISPLAY_ENABLED
#pragma region Display
//Display
#include "display.h"

TaskHandle_t display_TaskHandle = NULL;

#define DISPLAY_TASK_DELAY 50 //ms
#define DISPLAY_LINE_TWO_DELAY 10000 //ms
#define DISPLAY_LINE_THREE_DELAY 50000 //ms
#define DISPLAY_LINE_FOUR_DELAY 20000 //ms
#define DISPLAY_LINE_FIVE_DELAY 10000 //ms
#define DISPLAY_LINE_SIX_DELAY 10000 //ms
#define DISPLAY_LINE_ONE 10000 //ms

void display_Task(void* pvParameters) {

    uint32_t lastLineOneUpdate = 0;
    uint32_t lastLineTwoUpdate = 0;
    uint32_t lastLineThreeUpdate = 0;
#ifdef GPS_ENABLED
    uint32_t lastGPSUpdate = 0;
#endif
    uint32_t lastLineFourUpdate = 0;
    uint32_t lastLineFiveUpdate = 0;
    uint32_t lastLineSixUpdate = 0;
    uint32_t lastLineSevenUpdate = 0;

    while (true) {
        //Update line one every DISPLAY_LINE_ONE ms
        if (millis() - lastLineOneUpdate > DISPLAY_LINE_ONE) {
            lastLineOneUpdate = millis();
            // float batteryVoltage = getBatteryVoltage();
            // Given the previous float value, convert it into string with 2 decimal places
            bool isConnected = wiFiService.isConnected() || loraMeshService.hasGateway();
            String lineOne = "LoRaTRUST-  " + String(isConnected ? "CON" : "NC");

            Screen.changeLineOne(lineOne);
        }

        //Update line two every DISPLAY_LINE_TWO_DELAY ms
        if (millis() - lastLineTwoUpdate > DISPLAY_LINE_TWO_DELAY) {
            lastLineTwoUpdate = millis();
            String lineTwo = String(loraMeshService.getLocalAddress(), HEX);

            if (wiFiService.isConnected())
                lineTwo += " | " + wiFiService.getIP();

            Screen.changeLineTwo(lineTwo);
        }

#ifdef GPS_ENABLED
        //Update line three every DISPLAY_LINE_THREE_DELAY ms
        // if (millis() - lastLineThreeUpdate > DISPLAY_LINE_THREE_DELAY) {
        //     lastLineThreeUpdate = millis();
        //     String lineThree = gpsService.getGPSUpdatedWait();
        //     if (lineThree.begin() != "G")
        //         Screen.changeLineThree(lineThree);
        // }

        //Update GPS every UPDATE_GPS_DELAY ms
        if (millis() - lastGPSUpdate > UPDATE_GPS_DELAY) {
            lastGPSUpdate = millis();
            String lineThree = gpsService.getGPSUpdatedWait();
            if (lineThree.startsWith("G") != 1)
                Screen.changeLineThree(lineThree);
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
        2048,
        (void*) 1,
        2,
        &display_TaskHandle);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Display Task creation gave error: %d", res);
        createUpdateDisplay();
    }
}

void initDisplay() {
    Screen.initDisplay();
    createUpdateDisplay();
}
#pragma endregion
#endif

#pragma region Wire

void initWire() {
    Wire.begin((int) I2C_SDA, (int) I2C_SCL);
}

// TODO: The following line could be removed if we add the files in /src to /lib. However, at this moment, it generates a lot of errors
// TODO: https://docs.platformio.org/en/stable/advanced/unit-testing/structure/shared-code.html#unit-testing-shared-code

#ifndef PIO_UNIT_TESTING

void setup() {
    // Initialize Serial Monitor
    Serial.begin(115200);

    // Set log level
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    ESP_LOGV(TAG, "Build environment name: %s", BUILD_ENV_NAME);

    // Initialize Wire
    initWire();

    ESP_LOGV(TAG, "Heap before initManager: %d", ESP.getFreeHeap());

    // Initialize Manager
    initManager();

    ESP_LOGV(TAG, "Heap after initManager: %d", ESP.getFreeHeap());

#ifdef WIFI_ENABLED
    // Initialize WiFi
    initWiFi();
    ESP_LOGV(TAG, "Heap after initWiFi: %d", ESP.getFreeHeap());
#endif

#ifdef MQTT_ENABLED
    // Initialize MQTT
    initMQTT();
    ESP_LOGV(TAG, "Heap after initMQTT: %d", ESP.getFreeHeap());
#endif

    // Initialize AXP192
    // initAXP();

#ifdef GPS_ENABLED
    // Initialize GPS
    initGPS();
    ESP_LOGV(TAG, "Heap after initGPS: %d", ESP.getFreeHeap());
#endif

    // Initialize LoRaMesh
    initLoRaMesher();
    ESP_LOGV(TAG, "Heap after initLoRaMesher: %d", ESP.getFreeHeap());

#ifdef BLUETOOTH_ENABLED
    // Initialize Bluetooth
    initBluetooth();
    ESP_LOGV(TAG, "Heap after initBluetooth: %d", ESP.getFreeHeap());
#endif

#ifdef LED_ENABLED
    // Initialize Led
    initLed();
#endif

#ifdef METADATA_ENABLED
    // Initialize Metadata
    initMetadata();
    ESP_LOGV(TAG, "Heap after initMetadata: %d", ESP.getFreeHeap());
#endif

#ifdef SENSORS_ENABLED
    // Initialize Sensors
    initSensors();
    ESP_LOGV(TAG, "Heap after init Sensors: %d", ESP.getFreeHeap());
#endif

#ifdef DISPLAY_ENABLED
    // Initialize Display
    initDisplay();
    ESP_LOGV(TAG, "Heap after initDisplay: %d", ESP.getFreeHeap());
#endif


#ifdef SIMULATION_ENABLED
    // Initialize Simulator
    initSimulator();
#endif

#ifdef BATTERY_ENABLED
    // Initialize Battery
    initBattery();
#endif

    ESP_LOGV(TAG, "Setup finished");

#ifdef LED_ENABLED
    // Blink 2 times to show that the device is ready
    led.ledBlink();
#endif

#ifdef ROUTING_TABLE_RECORDING_ENABLED
    initRT();
    ESP_LOGV(TAG, "Heap after initRT: %d", ESP.getFreeHeap());
#endif

}

void loop() {
    vTaskDelay(200000 / portTICK_PERIOD_MS);

    Serial.printf("FREE HEAP: %d\n", ESP.getFreeHeap());
    Serial.printf("Min, Max: %d, %d\n", ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

#ifdef BATTERY_ENABLED
    if (battery.getVoltagePercentage() < 20) {
        ESP_LOGE(TAG, "Battery is low, deep sleeping for %d s", DEEP_SLEEP_TIME);
        mqttService.disconnect();
        wiFiService.disconnectWiFi();
        esp_wifi_deinit();

        ESP.deepSleep(DEEP_SLEEP_TIME * (uint32_t) 1000000);
    }
#endif

    if (ESP.getFreeHeap() < 20000) {
        ESP_LOGE(TAG, "Not enough memory to process mqtt messages");
        ESP.restart();
        return;
    }

    // if (millis() > 21600000) {
    //     ESP_LOGE(TAG, "Restarting device to avoid memory leaks");
    //     ESP.restart();
    // }
}

#endif