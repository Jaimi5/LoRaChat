#include <Arduino.h>

// Configuration
#include "config.h"

// Log
#include "esp32-hal-log.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"

// Manager
#include "message/messageManager.h"

// LoRaMesh
#include "loramesh/loraMeshService.h"

// WiFi
#include "wifi/wifiServerService.h"

// Display
#include "display/displayService.h"

// Devices
#include "devices/initDevices.h"

// OTA boot guard
#include "ota/otaBootGuard.h"

// MQTT
#include "mqtt/mqttService.h"

// Monitor
#include "monitor/monService.h"

static const char* TAG = "Main";

#pragma region Display
DisplayService& displayService = DisplayService::getInstance();
void initDisplay() {
    displayService.init();
}
#pragma endregion

#pragma region MQTT_MON
MonService& mon_mqttService = MonService::getInstance();
void init_mqtt_mon() {
    mon_mqttService.init();
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
    loraMeshService.initLoraMesherService();
}

#pragma endregion


#pragma region MQTT

MqttService& mqttService = MqttService::getInstance();

void initMQTT() {
    mqttService.initMqtt(String(loraMeshService.getLocalAddress()));
}

#pragma endregion


#pragma region Manager

MessageManager& manager = MessageManager::getInstance();

void initManager() {
    manager.init();

    manager.addMessageService(&loraMeshService);
#ifdef WIFI_ENABLED
    manager.addMessageService(&wiFiService);
#endif
#ifdef MQTT_ENABLED
    manager.addMessageService(&mqttService);
#endif
#ifdef MQTT_MON_ENABLED
    manager.addMessageService(&mon_mqttService);
#endif
#ifdef DISPLAY_ENABLED
    manager.addMessageService(&displayService);
#endif
}

#pragma endregion

#pragma region Wire

void initWire() {
    Wire.begin((int)I2C_SDA, (int)I2C_SCL);
}

#pragma endregion

#pragma region HeapReport

constexpr uint64_t HEAP_REPORT_PERIOD_US = 200ULL * 1000 * 1000;

void logHeap(void*) {
    ESP_LOGI(TAG, "FREE HEAP: %u", ESP.getFreeHeap());
    ESP_LOGI(TAG, "Min, Max: %u, %u", ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
}

void startHeapReport() {
    esp_timer_create_args_t args = {};
    args.callback = &logHeap;
    args.name = "heapReport";
    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&args, &timer) != ESP_OK ||
        esp_timer_start_periodic(timer, HEAP_REPORT_PERIOD_US) != ESP_OK) {
        ESP_LOGE(TAG, "Heap report timer could not be started");
    }
}

#pragma endregion

#ifndef PIO_UNIT_TESTING

// Stops initArduino() from marking a PENDING_VERIFY image valid before the boot guard decides.
extern "C" bool verifyRollbackLater() {
    return true;
}

OtaBootGuard& bootGuard = OtaBootGuard::getInstance();

void setup() {
    // Initialize Serial Monitor
    Serial.begin(115200);

    // Set log level. INFO keeps operational and error logs while dropping the
    // verbose/debug spam whose blocking UART flush (~7 ms/line at 115200) and
    // log-mutex contention starve time-critical tasks during reconnect storms.
    esp_log_level_set("*", ESP_LOG_INFO);

    bootGuard.begin();

    ESP_LOGI(TAG, "Build environment name: %s", BUILD_ENV_NAME);

    // Initialize Wire
    initWire();

    // Initialize Devices
    InitDevices::init();
    bootGuard.reportCheck(SelfTestCheck::PMU, InitDevices::pmuResponds());

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

#ifdef DISPLAY_ENABLED
    // Initialize Display
    initDisplay();
    ESP_LOGV(TAG, "Heap after initDisplay: %d", ESP.getFreeHeap());
#endif

    // Initialize LoRaMesh
    initLoRaMesher();
    ESP_LOGV(TAG, "Heap after initLoRaMesher: %d", ESP.getFreeHeap());
    bootGuard.reportCheck(SelfTestCheck::RADIO, loraMeshService.isRunning());

#ifdef WIFI_ENABLED
    // WiFi may have connected before LoRaMesher was initialized, causing
    // setGateway() to silently fail (mesher_ was NULL). Re-check now.
    if (wiFiService.isConnected()) {
        ESP_LOGI(TAG, "WiFi already connected at LoRaMesher init — setting gateway");
        loraMeshService.setGateway();
    }
#endif

#ifdef MQTT_MON_ENABLED
    // Initialize MQTT_MON
    init_mqtt_mon();
    ESP_LOGV(TAG, "Heap after init_mqtt_mon: %d", ESP.getFreeHeap());
#endif

    bootGuard.reportSetupDone();

    startHeapReport();

    ESP_LOGV(TAG, "Setup finished");
}

void loop() {
    // All work runs in service tasks; the Arduino loop task is not needed.
    vTaskDelete(NULL);
}

#endif