#include <Arduino.h>

// Configuration
#include "config.h"

// Log
#include "esp32-hal-log.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

// Manager
#include "message/messageManager.h"

// LoRaMesh
#include "loramesh/loraMeshService.h"

// WiFi
#include "wifi/wifiServerService.h"

// Sensors
#include "sensor/sensorService.h"

// Metadata
#include "sensor/metadata/metadata.h"

// Display
#include "display/displayService.h"

// Devices
#include "devices/initDevices.h"


static const char* TAG = "Main";

// Display
#pragma region Display
DisplayService& displayService = DisplayService::getInstance();
void initDisplay() {
    displayService.init();
}

#pragma endregion


#pragma region MQTT_MON
#include "monitor/monService.h"
MonService& mon_mqttService = MonService::getInstance();
void init_mqtt_mon() {
    mon_mqttService.init();
}
#pragma endregion

#pragma region OTA
#ifdef OTA_ENABLED
#include "ota/otaService.h"
OTAService& otaService = OTAService::getInstance();
void initOTA() {
    otaService.init();
}
#endif
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
#if defined(NODE_ACTIVE) && (NODE_ACTIVE == 0)
    ESP_LOGI(TAG, "NODE_ACTIVE=0: LoRa disabled, skipping mesh init");
    return;
#else
    // Init LoRaMesher
    loraMeshService.initLoraMesherService();
#endif
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
    // Initialize GPS
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

void initManager(bool printCommands) {
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

    manager.addMessageService(&mon_mqttService);
    ESP_LOGV(TAG, "MON-MQTT service added to manager");

    manager.addMessageService(&displayService);
    ESP_LOGV(TAG, "Display service added to manager");

#ifdef OTA_ENABLED
    manager.addMessageService(&otaService);
    ESP_LOGV(TAG, "OTA service added to manager");
#endif

    if (printCommands) {
        Serial.println(manager.getAvailableCommands());
    }
}

#pragma endregion

#pragma region Wire

void initWire() {
    Wire.begin((int)I2C_SDA, (int)I2C_SCL);
}

#pragma endregion

// TODO: The following line could be removed if we add the files in /src to /lib. However, at this
// moment, it generates a lot of errors
// TODO:
// https://docs.platformio.org/en/stable/advanced/unit-testing/structure/shared-code.html#unit-testing-shared-code

#ifndef PIO_UNIT_TESTING

void setup() {
    // Initialize Serial Monitor
    Serial.begin(115200);

    // Mark app valid so bootloader doesn't roll back
    esp_ota_mark_app_valid_cancel_rollback();

    // Set log level. INFO keeps operational and error logs while dropping the
    // verbose/debug spam whose blocking UART flush (~7 ms/line at 115200) and
    // log-mutex contention starve time-critical tasks during reconnect storms.
    esp_log_level_set("*", ESP_LOG_INFO);

    // Book a deep sleep that ended in this boot before anything logs HEALTH
    loraMeshService.onBoot();
    const esp_reset_reason_t resetReason = esp_reset_reason();
#ifdef USE_LORAMESHER_V2
    // No mesher yet: the address derived from the efuse MAC, as the mesher uses
    const uint16_t bootNode = loraMeshService.getLocalAddress();
#else
    const uint16_t bootNode = 0;
#endif
    ESP_LOGI(TAG, "BOOT node=0x%04X reason=%d app_ms=%lu", bootNode, (int)resetReason,
             (unsigned long)millis());

#if SIM_SLEEP_MODE == 2
    // A member back from deep sleep must be in its next active slot within
    // LORA_DEEP_SLEEP_BOOT_MS: start the mesh right after the PMU, and skip the
    // display and GPS
    const bool deepSleepWake = resetReason == ESP_RST_DEEPSLEEP;
#else
    const bool deepSleepWake = false;
#endif

    ESP_LOGI(TAG, "Build environment name: %s", BUILD_ENV_NAME);

    // Initialize Wire
    initWire();

    if (deepSleepWake) {
        InitDevices::initPower();
        // Services are registered before the mesh can deliver packets to them
        initManager(false);
        initLoRaMesher();
    } else {
        // Initialize Devices
        InitDevices::init();

        ESP_LOGV(TAG, "Heap before initManager: %d", ESP.getFreeHeap());

        // Initialize Manager
        initManager(true);

        ESP_LOGV(TAG, "Heap after initManager: %d", ESP.getFreeHeap());
    }

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
    if (!deepSleepWake) {
        // Initialize Display
        initDisplay();
        ESP_LOGV(TAG, "Heap after initDisplay: %d", ESP.getFreeHeap());
    }
#endif

#ifdef GPS_ENABLED
    if (!deepSleepWake) {
        // Initialize GPS
        initGPS();
        ESP_LOGV(TAG, "Heap after initGPS: %d", ESP.getFreeHeap());
    }
#endif

    if (!deepSleepWake) {
        // Initialize LoRaMesh
        initLoRaMesher();
        ESP_LOGV(TAG, "Heap after initLoRaMesher: %d", ESP.getFreeHeap());
    }

#ifdef WIFI_ENABLED
    // WiFi may have connected before LoRaMesher was initialized, causing
    // setGateway() to silently fail (mesher_ was NULL). Re-check now.
    if (wiFiService.isConnected()) {
        ESP_LOGI(TAG, "WiFi already connected at LoRaMesher init — setting gateway");
        loraMeshService.setGateway();
    }
#endif

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

#ifdef SIMULATION_ENABLED
    // Initialize Simulator
    initSimulator();
#endif

#ifdef BATTERY_ENABLED
    // Initialize Battery
    initBattery();
#endif

#ifdef MQTT_MON_ENABLED
    // Initialize MQTT_MON
    init_mqtt_mon();
    ESP_LOGV(TAG, "Heap after init_mqtt_mon: %d", ESP.getFreeHeap());
#endif

#ifdef OTA_ENABLED
    initOTA();
    ESP_LOGV(TAG, "Heap after initOTA: %d", ESP.getFreeHeap());
#endif

    ESP_LOGV(TAG, "Setup finished");

#ifdef LED_ENABLED
    // Blink 2 times to show that the device is ready
    led.ledBlink();
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

        ESP.deepSleep(DEEP_SLEEP_TIME * (uint32_t)1000000);
    }
#endif

    // if (ESP.getFreeHeap() < 20000) {
    //     ESP_LOGE(TAG, "Not enough memory to process mqtt messages");
    //     ESP.restart();
    //     return;
    // }

    // if (millis() > 21600000) {
    //     ESP_LOGE(TAG, "Restarting device to avoid memory leaks");
    //     ESP.restart();
    // }
}

#endif