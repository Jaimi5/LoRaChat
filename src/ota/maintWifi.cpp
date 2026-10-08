#include "maintWifi.h"

#include <algorithm>
#include <cstring>

#include "config.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "maintWindow.h"

static const char* MW_TAG = "MaintWifi";
static constexpr EventBits_t GOT_IP_BIT = BIT0;
static EventGroupHandle_t wifiEvents = nullptr;

static void onWifiEvent(void* arg, esp_event_base_t base, int32_t id, void*) {
    auto* self = static_cast<std::atomic<bool>*>(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!self->load()) esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifiEvents, GOT_IP_BIT);
    }
}

void MaintenanceWifi::startBootWindow(bool gateway, bool pendingVerify) {
    // Credentials are only known once the WiFi driver is up; the window task checks them.
    BootWindowDecision decision = MaintWindow::decideBootWindow(gateway, pendingVerify, true);
    if (decision != BootWindowDecision::OPEN) {
        ESP_LOGI(MW_TAG, "Boot window skipped (reason %u)", static_cast<unsigned>(decision));
        return;
    }
    gateway_ = gateway;
    pendingVerify_ = pendingVerify;
    if (xTaskCreate(windowTask, "MaintWifi", 4096, this, 2, nullptr) != pdPASS) {
        ESP_LOGE(MW_TAG, "Maintenance window task creation failed");
    }
}

void MaintenanceWifi::windowTask(void* parameter) {
    static_cast<MaintenanceWifi*>(parameter)->runWindow(MaintWindow::BOOT_WINDOW_MS);
    vTaskDelete(nullptr);
}

void MaintenanceWifi::runWindow(uint32_t durationMs) {
    unsigned long start = millis();
    bool hasCredentials = loadCredentials();
    BootWindowDecision decision =
        MaintWindow::decideBootWindow(gateway_, pendingVerify_, hasCredentials);
    if (decision != BootWindowDecision::OPEN) {
        ESP_LOGI(MW_TAG, "Boot window skipped (reason %u)", static_cast<unsigned>(decision));
        stopWifi();
        return;
    }
    ESP_LOGI(MW_TAG, "Window open for %u s, network %s", durationMs / 1000, ssid_);
    if (!startWifi()) {
        stopWifi();
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(wifiEvents, GOT_IP_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(durationMs));
    if (bits & GOT_IP_BIT) {
        wifi_ap_record_t ap = {};
        esp_wifi_sta_get_ap_info(&ap);
        unsigned long joinedMs = millis() - start;
        ESP_LOGI(MW_TAG, "Joined %s in %lu ms, RSSI %d dBm", ssid_, joinedMs, ap.rssi);
        if (work_) work_();
    } else {
        ESP_LOGI(MW_TAG, "Network %s not joined", ssid_);
    }

    stopWifi();
    unsigned long openMs = millis() - start;
    ESP_LOGI(MW_TAG, "Window closed after %lu ms", openMs);
}

bool MaintenanceWifi::loadCredentials() {
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(MW_TAG, "Network stack init failed: %s", esp_err_to_name(err));
        return false;
    }
    if (netif_ == nullptr) netif_ = esp_netif_create_default_wifi_sta();

    // With the default flash storage the driver loads the station config saved in NVS.
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        ESP_LOGE(MW_TAG, "WiFi init failed: %s", esp_err_to_name(err));
        return false;
    }
    wifi_config_t stored = {};
    esp_wifi_get_config(WIFI_IF_STA, &stored);
    const char* ssid = reinterpret_cast<const char*>(stored.sta.ssid);
    const char* password = reinterpret_cast<const char*>(stored.sta.password);
    if (ssid[0] == '\0') {
        ssid = WIFI_SSID;
        password = WIFI_PASSWORD;
    }
    std::strncpy(ssid_, ssid, sizeof(ssid_) - 1);
    std::strncpy(password_, password, sizeof(password_) - 1);
    return ssid_[0] != '\0';
}

bool MaintenanceWifi::startWifi() {
    stopping_ = false;
    if (wifiEvents == nullptr) wifiEvents = xEventGroupCreate();
    xEventGroupClearBits(wifiEvents, GOT_IP_BIT);

    wifi_config_t config = {};
    std::memcpy(config.sta.ssid, ssid_, std::min(std::strlen(ssid_), sizeof(config.sta.ssid)));
    std::memcpy(config.sta.password, password_,
                std::min(std::strlen(password_), sizeof(config.sta.password)));

    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) {
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifiEvent, &stopping_);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onWifiEvent, &stopping_);
    }
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(MW_TAG, "WiFi start failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void MaintenanceWifi::stopWifi() {
    stopping_ = true;
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifiEvent);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, onWifiEvent);
    esp_wifi_deinit();
}
