#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "string.h"
#include "esp32-hal-log.h"

#include "nvs.h"
#include "nvs_flash.h"

// Configuration
#include "config.h"

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

// Led
#include "led/led.h"

static const char* TAG = "Main";

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
    ESP_LOGI(TAG, "Manager initialized");

    manager.addMessageService(&loraMeshService);
    ESP_LOGI(TAG, "LoRaMesher service added to manager");

    manager.addMessageService(&wiFiService);
    ESP_LOGI(TAG, "WiFi service added to manager");

    manager.addMessageService(&mqttService);
    ESP_LOGI(TAG, "Mqtt service added to manager");

#ifdef LED_ENABLED
    manager.addMessageService(&led);
    ESP_LOGI(TAG, "Led service added to manager");
#endif

#ifdef BLUETOOTH_ENABLED
    manager.addMessageService(&bluetoothService);
    ESP_LOGI(TAG, "Bluetooth service added to manager");
#endif

#ifdef SIMULATION_ENABLED
    manager.addMessageService(&simulator);
    ESP_LOGI(TAG, "Simulator service added to manager");
#endif

    ESP_LOGI(TAG, "%s", manager.getAvailableCommands().c_str());
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
        (void*) 1,
        2,
        &display_TaskHandle);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Display Task creation gave error: %d", res);
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

    // Set log level
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    // Blink 2 times to show that the device is ready
    Helper::ledBlink(2, 100);

    // Initialize Display
    initDisplay();

    ESP_LOGI(TAG, "Free ram before starting Manager %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Initialize Manager
    initManager();

    ESP_LOGI(TAG, "Free ram before starting WiFi %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Initialize WiFi
    initWiFi();

    vTaskDelay(10000 / portTICK_PERIOD_MS);

#ifdef BLUETOOTH_ENABLED

    ESP_LOGI(TAG, "Free ram before starting BLE %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    initBluetooth();

#endif

    ESP_LOGI(TAG, "Free ram before starting mqtt %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Initialize Mqtt
    initMqtt();

    ESP_LOGI(TAG, "Free ram before starting Display %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

#ifdef LED_ENABLED
    // Initialize Led
    initLed();
#endif

    ESP_LOGI(TAG, "Free ram before starting Simulator %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

#ifdef SIMULATION_ENABLED
    // Initialize Simulator
    initSimulator();
#endif

    vTaskDelay(60000 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Free ram before starting LoRaMesher %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Initialize LoRaMesh
    initLoRaMesher();

    // Blink 2 times to show that the device is ready
    Helper::ledBlink(2, 100);
}

void loop() {
    // Suspend this task
    vTaskSuspend(NULL);
}