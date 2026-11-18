#include "nodeService.h"

#include "config.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "ota/maintWifi.h"
#include "ota/otaBootGuard.h"

static const char* NODE_TAG = "NodeService";
static const char* NVS_NAMESPACE = "lmnode";
static const char* NVS_KEY_ROLE = "role";
static const char* NVS_KEY_SLEEP_MODE = "sleep";
static const char* NVS_KEY_SLEEP_FRACTION = "sleepfrac";
static constexpr uint64_t RESTART_DELAY_US = 1000ULL * 1000;

void NodeService::init(uint16_t address) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t value = 0;
        stored_ = nvs_get_u8(handle, NVS_KEY_ROLE, &value) == ESP_OK &&
                  nodeRoleFromValue(value, role_);
        sleepModeStored_ = nvs_get_u8(handle, NVS_KEY_SLEEP_MODE, &value) == ESP_OK &&
                           sleepModeFromValue(value, sleepMode_);
        if (nvs_get_u8(handle, NVS_KEY_SLEEP_FRACTION, &value) == ESP_OK &&
            value <= MAX_SLEEP_FRACTION_PERCENT) {
            sleepFraction_ = value;
        }
        nvs_close(handle);
    }
    if (!stored_) role_ = defaultNodeRole(address, LORA_MANAGER_ID);
    ESP_LOGI(NODE_TAG, "Node role: %s, %s", describeRole().c_str(), describeSleep().c_str());
}

String NodeService::describeRole() const {
    return String(nodeRoleName(role_)) + (stored_ ? " (stored)" : " (default)");
}

CommandReply NodeService::setRole(const String& text) {
    NodeRole role;
    String name = text;
    name.trim();
    if (!parseNodeRole(name.c_str(), role)) return CommandStatus::ARG;
    return storeAndRestart(NVS_KEY_ROLE, static_cast<uint8_t>(role),
                           String("Role set to ") + nodeRoleName(role));
}

String NodeService::describeSleep() const {
    String text = String("sleep ") + sleepModeName(sleepMode_) +
                  (sleepModeStored_ ? " (stored)" : " (default)") + ", fraction ";
    if (sleepFraction_ < 0) return text + "default";
    return text + sleepFraction_ + " % (stored)";
}

CommandReply NodeService::setSleepMode(const String& text) {
    SleepMode mode;
    String name = text;
    name.trim();
    if (!parseSleepMode(name.c_str(), mode)) return CommandStatus::ARG;
    return storeAndRestart(NVS_KEY_SLEEP_MODE, static_cast<uint8_t>(mode),
                           String("Sleep mode set to ") + sleepModeName(mode));
}

CommandReply NodeService::setSleepFraction(const String& text) {
    uint8_t percent = 0;
    String value = text;
    value.trim();
    if (!parseSleepFraction(value.c_str(), percent)) return CommandStatus::ARG;
    return storeAndRestart(NVS_KEY_SLEEP_FRACTION, percent,
                           String("Sleep fraction set to ") + percent + " %");
}

CommandReply NodeService::storeAndRestart(const char* key, uint8_t value, const String& done) {
    if (OtaBootGuard::getInstance().isPendingVerify()) {
        return CommandReply(NODE_ERR_VERIFYING, "");
    }

    nvs_handle_t handle;
    esp_err_t opened = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (opened != ESP_OK) return CommandReply(NODE_ERR_STORE, esp_err_to_name(opened));
    esp_err_t err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return CommandReply(NODE_ERR_STORE, esp_err_to_name(err));

    esp_timer_create_args_t args = {};
    args.callback = [](void*) { esp_restart(); };
    args.name = "nodeRestart";
    MaintenanceWifi::getInstance().saveClock();
    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&args, &timer) == ESP_OK) esp_timer_start_once(timer, RESTART_DELAY_US);
    ESP_LOGI(NODE_TAG, "%s, restarting", done.c_str());
    return String(done + ", restarting");
}
