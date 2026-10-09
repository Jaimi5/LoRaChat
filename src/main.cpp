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

// Node role
#include "node/nodeService.h"

// Commands
#include "commands/commandRouter.h"
#include "commands/serialConsole.h"

// Maintenance WiFi and OTA
#include "ota/maintWifi.h"
#include "ota/otaService.h"
#include "ota/otaWifiPull.h"

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

void initLoRaMesher(bool networkManager) {
    loraMeshService.initLoraMesherService(networkManager);
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
NodeService& nodeService = NodeService::getInstance();

void initManager(bool gateway) {
    manager.init();

    manager.addMessageService(&nodeService);
    manager.addMessageService(&OtaService::getInstance());
    manager.addMessageService(&CommandRouter::getInstance());
    manager.addMessageService(&loraMeshService);
#ifdef WIFI_ENABLED
    if (gateway) manager.addMessageService(&wiFiService);
#endif
#ifdef MQTT_ENABLED
    if (gateway) manager.addMessageService(&mqttService);
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
#ifdef DISPLAY_ENABLED
    InitDevices::onPowerKeyShortPress([] { displayService.wake(); });
#endif
    InitDevices::init();
    bootGuard.reportCheck(SelfTestCheck::PMU, InitDevices::pmuResponds());

    // The role decides which services run: a gateway runs WiFi, MQTT and the mesh manager
    nodeService.init(loraMeshService.getLocalAddress());
    bool gateway = nodeService.isGateway();

    initManager(gateway);

#ifdef WIFI_ENABLED
    if (gateway) initWiFi();
#endif

#ifdef MQTT_ENABLED
    if (gateway) initMQTT();
#endif

#ifdef DISPLAY_ENABLED
    // Initialize Display
    initDisplay();
    ESP_LOGV(TAG, "Heap after initDisplay: %d", ESP.getFreeHeap());
#endif

    // Initialize LoRaMesh
    initLoRaMesher(gateway);
    ESP_LOGV(TAG, "Heap after initLoRaMesher: %d", ESP.getFreeHeap());
    bootGuard.reportCheck(SelfTestCheck::RADIO, loraMeshService.isRunning());

#ifdef WIFI_ENABLED
    // WiFi may have connected before LoRaMesher was initialized, causing
    // setGateway() to silently fail (mesher_ was NULL). Re-check now.
    if (gateway && wiFiService.isConnected()) {
        ESP_LOGI(TAG, "WiFi already connected at LoRaMesher init — setting gateway");
        loraMeshService.setGateway();
    }
#endif

#ifdef MQTT_MON_ENABLED
    // Initialize MQTT_MON
    init_mqtt_mon();
    ESP_LOGV(TAG, "Heap after init_mqtt_mon: %d", ESP.getFreeHeap());
#endif

    CommandRouter::getInstance().begin();
    SerialConsole::begin();

    MaintenanceWifi::getInstance().begin();
    MaintenanceWifi::getInstance().onConnected(OtaWifiPull::run);
    MaintenanceWifi::getInstance().startBootWindow(gateway, bootGuard.isPendingVerify());

    bootGuard.reportSetupDone();

    startHeapReport();

    ESP_LOGV(TAG, "Setup finished");
}

void loop() {
    // All work runs in service tasks; the Arduino loop task is not needed.
    vTaskDelete(NULL);
}

#endif