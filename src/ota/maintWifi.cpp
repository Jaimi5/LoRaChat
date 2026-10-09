#include "maintWifi.h"

#include <algorithm>
#include <cstring>

#include "config.h"
#include "deploymentKey.h"
#include "devices/initDevices.h"
#include "esp_timer.h"
#include "nvs.h"
#include "otaPolicy.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "loramesh/loraMeshService.h"
#include "maintWindow.h"
#include "otaBootGuard.h"
#include "otaService.h"
#include "otaUploadServer.h"
#include "wifi/wifiServerService.h"

static const char* MW_TAG = "MaintWifi";
static constexpr EventBits_t GOT_IP_BIT = BIT0;
static constexpr EventBits_t CLOSE_BIT = BIT1;
static EventGroupHandle_t wifiEvents = nullptr;
// The work runs in this task: HTTP client, ECDSA verification and flash writes.
static constexpr uint32_t WINDOW_TASK_STACK = 8192;
static constexpr uint8_t AP_CHANNEL = 1;
static constexpr uint8_t AP_MAX_CLIENTS = 2;
static const char* NVS_NAMESPACE = "lmmaint";
static const char* NVS_KEY_CLOCK = "clk";
static const char* NVS_KEY_PLAN = "plan";
static constexpr uint64_t CLOCK_TICK_US = 60ULL * 1000 * 1000;
static constexpr uint32_t WAIT_STEP_MS = 1000;
static constexpr uint32_t VERDICT_POLL_MS = 1000;
static constexpr int CLOSE_WAIT_STEPS = 100;

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

void MaintenanceWifi::begin() {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u32(handle, NVS_KEY_CLOCK, &clockAtBoot_);
        uint8_t blob[64];
        size_t size = sizeof(blob);
        if (nvs_get_blob(handle, NVS_KEY_PLAN, blob, &size) == ESP_OK &&
            !MaintWindowPlan::decode(blob, size, plan_)) {
            ESP_LOGE(MW_TAG, "Stored window plan is corrupt, ignoring it");
        }
        nvs_close(handle);
    }
    clockSavedAt_ = clockAtBoot_;

    esp_timer_create_args_t args = {};
    args.callback = clockTick;
    args.arg = this;
    args.name = "maintClock";
    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&args, &timer) != ESP_OK ||
        esp_timer_start_periodic(timer, CLOCK_TICK_US) != ESP_OK) {
        ESP_LOGE(MW_TAG, "Node clock timer could not be started");
    }
}

void MaintenanceWifi::startBootWindow(bool gateway, bool pendingVerify) {
    gateway_ = gateway;
    pendingVerify_ = pendingVerify;
    if (wifiEvents == nullptr) wifiEvents = xEventGroupCreate();

    WindowKind planned;
    {
        std::lock_guard<std::mutex> lock(planMutex_);
        planned = plan_.active(nodeNow());
    }
    if (planned != WindowKind::NONE && !gateway) {
        busy_ = true;
        kind_ = planned == WindowKind::AP ? Kind::AP : Kind::PULL;
        if (xTaskCreate(resumeTask, "MaintWifi", WINDOW_TASK_STACK, this, 2, nullptr) != pdPASS) {
            busy_ = false;
            ESP_LOGE(MW_TAG, "Maintenance window task creation failed");
        }
        return;
    }

    // Credentials are only known once the WiFi driver is up; the window task checks them.
    BootWindowDecision decision = MaintWindow::decideBootWindow(gateway, pendingVerify, true);
    if (decision != BootWindowDecision::OPEN) {
        ESP_LOGI(MW_TAG, "Boot window skipped (reason %u)", static_cast<unsigned>(decision));
        return;
    }
    bool idle = false;
    if (!busy_.compare_exchange_strong(idle, true)) return;
    bootWindowEndUs_ = esp_timer_get_time() + int64_t{MaintWindow::BOOT_WINDOW_MS} * 1000;
    if (!startTask(Kind::BOOT)) busy_ = false;
}

std::string MaintenanceWifi::closeWindow() {
    {
        std::lock_guard<std::mutex> lock(planMutex_);
        plan_.close();
        savePlan();
    }
    if (!busy_) return "No window open";
    xEventGroupSetBits(wifiEvents, CLOSE_BIT);
    return "Closing the window";
}

void MaintenanceWifi::saveClock() {
    uint32_t now = nodeNow();
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_u32(handle, NVS_KEY_CLOCK, now) == ESP_OK && nvs_commit(handle) == ESP_OK) {
        clockSavedAt_ = now;
    }
    nvs_close(handle);
}

void MaintenanceWifi::clockTick(void* parameter) {
    auto* self = static_cast<MaintenanceWifi*>(parameter);
    bool due;
    {
        std::lock_guard<std::mutex> lock(self->planMutex_);
        due = self->plan_.clockSaveDue(self->nodeNow(), self->clockSavedAt_);
    }
    if (due) self->saveClock();
}

uint32_t MaintenanceWifi::nodeNow() const {
    return clockAtBoot_ + static_cast<uint32_t>(esp_timer_get_time() / 1000000);
}

void MaintenanceWifi::savePlan() {
    std::vector<uint8_t> blob = plan_.encode();
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    esp_err_t err = nvs_set_blob(handle, NVS_KEY_PLAN, blob.data(), blob.size());
    if (err == ESP_OK) err = nvs_set_u32(handle, NVS_KEY_CLOCK, nodeNow());
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        clockSavedAt_ = nodeNow();
    } else {
        ESP_LOGE(MW_TAG, "Cannot save the window plan: %s", esp_err_to_name(err));
    }
}

std::string MaintenanceWifi::storeCredentials(const std::string& ssid,
                                              const std::string& password) {
    bool idle = false;
    if (!busy_.compare_exchange_strong(idle, true)) {
        return "A maintenance window is open; close it first";
    }
    wifi_config_t config = {};
    std::memcpy(config.sta.ssid, ssid.data(), std::min(ssid.size(), sizeof(config.sta.ssid)));
    std::memcpy(config.sta.password, password.data(),
                std::min(password.size(), sizeof(config.sta.password)));

    esp_err_t err = initWifi(false) ? ESP_OK : ESP_FAIL;
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &config);
    kind_ = Kind::PULL;
    stopWifi();
    busy_ = false;
    if (err != ESP_OK) {
        ESP_LOGE(MW_TAG, "Storing the WiFi credentials failed: %s", esp_err_to_name(err));
        return "Could not store the WiFi credentials";
    }
    ESP_LOGI(MW_TAG, "WiFi credentials stored for %s", ssid.c_str());
    return "WiFi credentials stored for " + ssid;
}

std::string MaintenanceWifi::open(Kind kind, uint32_t durationS) {
    if (gateway_) {
        if (kind == Kind::AP) {
            return "Gateways keep their own WiFi; the access point is for sensors";
        }
        return checkOverGatewayWifi();
    }
    if (OtaBootGuard::getInstance().isPendingVerify()) {
        return "The image is still being verified; try again when it is valid";
    }
    if (kind == Kind::AP) {
        DeploymentKey key;
        if (!OtaService::getInstance().deploymentKey(key)) {
            return "No deployment key; store one with /key.set";
        }
    }
    PowerState power;
    InitDevices::readPower(power);
    PolicyDecision resources =
        OtaPolicy::checkResources(power.batteryMv, power.externalPower, esp_get_free_heap_size());
    if (resources != PolicyDecision::INSTALL) {
        return std::string("Refused: ") + policyDecisionName(resources);
    }

    WindowKind windowKind = kind == Kind::AP ? WindowKind::AP : WindowKind::PULL;
    auto refusal = [](OpenDecision decision) -> std::string {
        if (decision == OpenDecision::REFUSED_LIMIT) {
            return "Refused: " + std::to_string(MaintWindowPlan::MAX_PER_DAY) +
                   " windows in the last 24 h";
        }
        if (decision == OpenDecision::REFUSED_DURATION) return "Refused: duration out of range";
        return "";
    };
    // A trial copy answers refusals before anything changes; the plan itself only changes once
    // this window owns the WiFi.
    OpenDecision trial;
    {
        std::lock_guard<std::mutex> lock(planMutex_);
        MaintWindowPlan copy = plan_;
        trial = copy.open(windowKind, durationS, nodeNow());
    }
    std::string refused = refusal(trial);
    if (!refused.empty()) return refused;
    if (trial == OpenDecision::EXTENDED && busy_ && kind_ == kind) {
        std::lock_guard<std::mutex> lock(planMutex_);
        uint32_t now = nodeNow();
        MaintWindowPlan extended = plan_;
        if (extended.open(windowKind, durationS, now) == OpenDecision::EXTENDED) {
            plan_ = extended;
            savePlan();
            return "Window extended, " + std::to_string(plan_.remaining(now)) + " s left";
        }
    }

    // Another window (boot window or the other kind) gives way to this one.
    if (busy_) {
        xEventGroupSetBits(wifiEvents, CLOSE_BIT);
        for (int i = 0; busy_ && i < CLOSE_WAIT_STEPS; i++) vTaskDelay(pdMS_TO_TICKS(100));
    }
    bool idle = false;
    if (!busy_.compare_exchange_strong(idle, true)) return "The open window did not close";

    MaintWindowPlan before;
    OpenDecision decision;
    uint32_t remainingS;
    size_t counted;
    {
        std::lock_guard<std::mutex> lock(planMutex_);
        uint32_t now = nodeNow();
        before = plan_;
        decision = plan_.open(windowKind, durationS, now);
        remainingS = plan_.remaining(now);
        counted = plan_.countedInLastDay(now);
        if (refusal(decision).empty()) savePlan();
    }
    refused = refusal(decision);
    if (!refused.empty()) {
        busy_ = false;
        return refused;
    }
    if (!startTask(kind)) {
        {
            std::lock_guard<std::mutex> lock(planMutex_);
            plan_ = before;
            savePlan();
        }
        busy_ = false;
        return "Maintenance window task creation failed";
    }
    std::string counts = " (" + std::to_string(counted) + " of " +
                         std::to_string(MaintWindowPlan::MAX_PER_DAY) + " in 24 h)";
    if (kind == Kind::AP) {
        return "Access point " + apSsid(LoRaMeshService::getInstance().getLocalAddress()) +
               " open for " + std::to_string(remainingS) +
               " s, upload at http://192.168.4.1/" + counts;
    }
    return "Pull window open for " + std::to_string(remainingS) + " s" + counts;
}

std::string MaintenanceWifi::checkOverGatewayWifi() {
    if (!WiFiServerService::getInstance().isConnected()) return "Gateway WiFi not connected";
    bool idle = false;
    if (!busy_.compare_exchange_strong(idle, true)) return "An update check is already running";
    auto task = [](void* parameter) {
        auto* self = static_cast<MaintenanceWifi*>(parameter);
        if (self->work_) self->work_();
        self->busy_ = false;
        vTaskDelete(nullptr);
    };
    if (xTaskCreate(task, "GatewayOta", WINDOW_TASK_STACK, this, 2, nullptr) != pdPASS) {
        busy_ = false;
        return "Update check task creation failed";
    }
    return "Checking the OTA server over the gateway WiFi";
}

bool MaintenanceWifi::startTask(Kind kind) {
    if (wifiEvents == nullptr) wifiEvents = xEventGroupCreate();
    xEventGroupClearBits(wifiEvents, GOT_IP_BIT | CLOSE_BIT);
    kind_ = kind;
    return xTaskCreate(windowTask, "MaintWifi", WINDOW_TASK_STACK, this, 2, nullptr) == pdPASS;
}

void MaintenanceWifi::windowTask(void* parameter) {
    auto* self = static_cast<MaintenanceWifi*>(parameter);
    self->runWindow();
    self->busy_ = false;
    vTaskDelete(nullptr);
}

void MaintenanceWifi::resumeTask(void* parameter) {
    auto* self = static_cast<MaintenanceWifi*>(parameter);
    OtaBootGuard& guard = OtaBootGuard::getInstance();
    while (guard.isPendingVerify()) vTaskDelay(pdMS_TO_TICKS(VERDICT_POLL_MS));
    {
        std::lock_guard<std::mutex> lock(self->planMutex_);
        uint32_t now = self->nodeNow();
        if (self->kind_ == Kind::AP && guard.attemptResolved()) {
            self->plan_.onResult(now);
            self->savePlan();
        }
        ESP_LOGI(MW_TAG, "Resuming the %s window, %u s left",
                 self->kind_ == Kind::AP ? "access point" : "pull", self->plan_.remaining(now));
    }
    if (self->remainingMs() > 0) {
        xEventGroupClearBits(wifiEvents, GOT_IP_BIT | CLOSE_BIT);
        self->runWindow();
    }
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

uint32_t MaintenanceWifi::remainingMs() {
    if (kind_ == Kind::BOOT) {
        int64_t left = bootWindowEndUs_ - esp_timer_get_time();
        return left > 0 ? static_cast<uint32_t>(left / 1000) : 0;
    }
    std::lock_guard<std::mutex> lock(planMutex_);
    uint32_t now = nodeNow();
    WindowKind running = kind_ == Kind::AP ? WindowKind::AP : WindowKind::PULL;
    return plan_.active(now) == running ? plan_.remaining(now) * 1000 : 0;
}

EventBits_t MaintenanceWifi::waitInWindow(EventBits_t bits) {
    for (;;) {
        uint32_t left = remainingMs();
        if (left == 0) return 0;
        EventBits_t got = xEventGroupWaitBits(wifiEvents, bits | CLOSE_BIT, pdFALSE, pdFALSE,
                                              pdMS_TO_TICKS(std::min(left, WAIT_STEP_MS)));
        if (got & (bits | CLOSE_BIT)) return got;
    }
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
    ESP_LOGI(MW_TAG, "Window open for %u s, network %s", remainingMs() / 1000, ssid_);
    if (!startStation()) return;

    EventBits_t bits = waitInWindow(GOT_IP_BIT);
    if (bits & CLOSE_BIT) {
        ESP_LOGI(MW_TAG, "Window closed by command");
    } else if (bits & GOT_IP_BIT) {
        wifi_ap_record_t ap = {};
        esp_wifi_sta_get_ap_info(&ap);
        unsigned long joinedMs = millis() - start;
        ESP_LOGI(MW_TAG, "Joined %s in %lu ms, RSSI %d dBm", ssid_, joinedMs, ap.rssi);
        if (work_) work_();
        // The update check is done: a pull window ends here.
        if (kind_ == Kind::PULL) {
            std::lock_guard<std::mutex> lock(planMutex_);
            plan_.close();
            savePlan();
        }
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
             ssid.c_str(), remainingMs() / 1000);

    if (waitInWindow(0) & CLOSE_BIT) ESP_LOGI(MW_TAG, "Window closed by command");
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
