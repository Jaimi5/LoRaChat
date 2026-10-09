#include "maintWifi.h"

#include <algorithm>
#include <cstring>

#include "config.h"
#include "deploymentKey.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "loramesh/loraMeshService.h"
#include "maintWindow.h"
#include "otaBootGuard.h"
#include "otaService.h"
#include "otaUploadServer.h"

static const char* MW_TAG = "MaintWifi";
static constexpr EventBits_t GOT_IP_BIT = BIT0;
static constexpr EventBits_t CLOSE_BIT = BIT1;
static EventGroupHandle_t wifiEvents = nullptr;
// The work runs in this task: HTTP client, ECDSA verification and flash writes.
static constexpr uint32_t WINDOW_TASK_STACK = 8192;
static constexpr uint8_t AP_CHANNEL = 1;
static constexpr uint8_t AP_MAX_CLIENTS = 2;

static void onWifiEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    auto* stopping = static_cast<std::atomic<bool>*>(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!stopping->load()) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        auto* event = static_cast<wifi_event_ap_staconnected_t*>(data);
        ESP_LOGI(MW_TAG, "Client " MACSTR " joined the access point", MAC2STR(event->mac));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        auto* event = static_cast<wifi_event_ap_stadisconnected_t*>(data);
        ESP_LOGI(MW_TAG, "Client " MACSTR " left the access point", MAC2STR(event->mac));
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifiEvents, GOT_IP_BIT);
    }
}

void MaintenanceWifi::startBootWindow(bool gateway, bool pendingVerify) {
    gateway_ = gateway;
    pendingVerify_ = pendingVerify;
    // Credentials are only known once the WiFi driver is up; the window task checks them.
    BootWindowDecision decision = MaintWindow::decideBootWindow(gateway, pendingVerify, true);
    if (decision != BootWindowDecision::OPEN) {
        ESP_LOGI(MW_TAG, "Boot window skipped (reason %u)", static_cast<unsigned>(decision));
        return;
    }
    std::string result = open(Kind::BOOT, MaintWindow::BOOT_WINDOW_MS);
    if (!result.empty()) ESP_LOGW(MW_TAG, "%s", result.c_str());
}

std::string MaintenanceWifi::openPullWindow(uint32_t durationMs) {
    std::string refused = open(Kind::PULL, durationMs);
    if (!refused.empty()) return refused;
    return "Pull window open for " + std::to_string(durationMs / 1000) + " s";
}

std::string MaintenanceWifi::openApWindow(uint32_t durationMs) {
    std::string refused = open(Kind::AP, durationMs);
    if (!refused.empty()) return refused;
    return "Access point " + apSsid(LoRaMeshService::getInstance().getLocalAddress()) +
           " open for " + std::to_string(durationMs / 1000) + " s, upload at http://192.168.4.1/";
}

std::string MaintenanceWifi::closeWindow() {
    if (!busy_) return "No window open";
    xEventGroupSetBits(wifiEvents, CLOSE_BIT);
    return "Closing the window";
}

std::string MaintenanceWifi::open(Kind kind, uint32_t durationMs) {
    if (kind != Kind::BOOT) {
        if (gateway_) return "Gateways keep their own WiFi; no maintenance window";
        if (OtaBootGuard::getInstance().isPendingVerify()) {
            return "The image is still being verified; try again when it is valid";
        }
    }
    if (kind == Kind::AP) {
        DeploymentKey key;
        if (!OtaService::getInstance().deploymentKey(key)) {
            return "No deployment key; store one with /key.set";
        }
    }
    bool idle = false;
    if (!busy_.compare_exchange_strong(idle, true)) return "A maintenance window is already open";

    if (wifiEvents == nullptr) wifiEvents = xEventGroupCreate();
    xEventGroupClearBits(wifiEvents, GOT_IP_BIT | CLOSE_BIT);
    kind_ = kind;
    durationMs_ = durationMs;
    if (xTaskCreate(windowTask, "MaintWifi", WINDOW_TASK_STACK, this, 2, nullptr) != pdPASS) {
        busy_ = false;
        return "Maintenance window task creation failed";
    }
    return "";
}

void MaintenanceWifi::windowTask(void* parameter) {
    auto* self = static_cast<MaintenanceWifi*>(parameter);
    self->runWindow();
    self->busy_ = false;
    vTaskDelete(nullptr);
}

void MaintenanceWifi::runWindow() {
    unsigned long start = millis();
    if (kind_ == Kind::AP) {
        runApWindow();
    } else {
        runStationWindow();
    }
    stopWifi();
    unsigned long openMs = millis() - start;
    ESP_LOGI(MW_TAG, "Window closed after %lu ms", openMs);
}

void MaintenanceWifi::runStationWindow() {
    unsigned long start = millis();
    bool hasCredentials = initWifi(false) && loadCredentials();
    if (kind_ == Kind::BOOT) {
        BootWindowDecision decision =
            MaintWindow::decideBootWindow(gateway_, pendingVerify_, hasCredentials);
        if (decision != BootWindowDecision::OPEN) {
            ESP_LOGI(MW_TAG, "Boot window skipped (reason %u)", static_cast<unsigned>(decision));
            return;
        }
    } else if (!hasCredentials) {
        ESP_LOGW(MW_TAG, "No WiFi credentials, window not opened");
        return;
    }
    ESP_LOGI(MW_TAG, "Window open for %u s, network %s", durationMs_ / 1000, ssid_);
    if (!startStation()) return;

    EventBits_t bits = xEventGroupWaitBits(wifiEvents, GOT_IP_BIT | CLOSE_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(durationMs_));
    if (bits & CLOSE_BIT) {
        ESP_LOGI(MW_TAG, "Window closed by command");
    } else if (bits & GOT_IP_BIT) {
        wifi_ap_record_t ap = {};
        esp_wifi_sta_get_ap_info(&ap);
        unsigned long joinedMs = millis() - start;
        ESP_LOGI(MW_TAG, "Joined %s in %lu ms, RSSI %d dBm", ssid_, joinedMs, ap.rssi);
        if (work_) work_();
    } else {
        ESP_LOGI(MW_TAG, "Network %s not joined", ssid_);
    }
}

void MaintenanceWifi::runApWindow() {
    DeploymentKey key;
    if (!OtaService::getInstance().deploymentKey(key)) return;
    uint16_t address = LoRaMeshService::getInstance().getLocalAddress();
    std::string ssid = apSsid(address);
    if (!initWifi(true) || !startAccessPoint(ssid, apPassword(key, address))) return;
    if (!OtaUploadServer::start()) return;
    ESP_LOGI(MW_TAG, "Access point %s open for %u s, upload at http://192.168.4.1/",
             ssid.c_str(), durationMs_ / 1000);

    EventBits_t bits =
        xEventGroupWaitBits(wifiEvents, CLOSE_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(durationMs_));
    if (bits & CLOSE_BIT) ESP_LOGI(MW_TAG, "Window closed by command");
    OtaUploadServer::stop();
}

bool MaintenanceWifi::initWifi(bool accessPoint) {
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(MW_TAG, "Network stack init failed: %s", esp_err_to_name(err));
        return false;
    }
    netif_ = accessPoint ? esp_netif_create_default_wifi_ap() : esp_netif_create_default_wifi_sta();

    // With the default flash storage the driver loads the station config saved in NVS.
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        ESP_LOGE(MW_TAG, "WiFi init failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool MaintenanceWifi::loadCredentials() {
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

bool MaintenanceWifi::startStation() {
    wifi_config_t config = {};
    std::memcpy(config.sta.ssid, ssid_, std::min(std::strlen(ssid_), sizeof(config.sta.ssid)));
    std::memcpy(config.sta.password, password_,
                std::min(std::strlen(password_), sizeof(config.sta.password)));

    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK && !registerEvents()) err = ESP_FAIL;
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(MW_TAG, "WiFi start failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool MaintenanceWifi::startAccessPoint(const std::string& ssid, const std::string& password) {
    wifi_config_t config = {};
    std::memcpy(config.ap.ssid, ssid.data(), std::min(ssid.size(), sizeof(config.ap.ssid)));
    config.ap.ssid_len = static_cast<uint8_t>(ssid.size());
    std::memcpy(config.ap.password, password.data(),
                std::min(password.size(), sizeof(config.ap.password) - 1));
    config.ap.channel = AP_CHANNEL;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.max_connection = AP_MAX_CLIENTS;

    esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK && !registerEvents()) err = ESP_FAIL;
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(MW_TAG, "Access point start failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool MaintenanceWifi::registerEvents() {
    stopping_ = false;
    return esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifiEvent, &stopping_) ==
               ESP_OK &&
           esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onWifiEvent, &stopping_) ==
               ESP_OK;
}

void MaintenanceWifi::stopWifi() {
    stopping_ = true;
    if (kind_ != Kind::AP) esp_wifi_disconnect();
    esp_wifi_stop();
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifiEvent);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, onWifiEvent);
    esp_wifi_deinit();
    // esp_wifi_deinit() clears the default WiFi handlers; the next window creates a new netif.
    if (netif_ != nullptr) esp_netif_destroy_default_wifi(netif_);
    netif_ = nullptr;
}
