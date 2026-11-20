#include "nodeService.h"

#include "config.h"
#include "deferredRestart.h"
#include "nvs.h"
#include "ota/otaBootGuard.h"

static const char* NODE_TAG = "NodeService";
static const char* NVS_NAMESPACE = "lmnode";
static const char* NVS_KEY_ROLE = "role";
static const char* NVS_KEY_SLEEP_MODE = "sleep";
static const char* NVS_KEY_SLEEP_FRACTION = "sleepfrac";

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

TypedResult NodeService::roleInto(Fields& out) const {
    enum Field : size_t { ROLE, SOURCE };
    out.setU(ROLE, static_cast<uint32_t>(role_));
    out.setU(SOURCE, stored_ ? 1 : 0);
    return TypedResult();
}

TypedResult NodeService::setRole(uint32_t role) {
    NodeRole parsed;
    if (role > UINT8_MAX || !nodeRoleFromValue(static_cast<uint8_t>(role), parsed)) {
        return CommandStatus::ARG;
    }
    return storeAndRestart(NVS_KEY_ROLE, static_cast<uint8_t>(parsed));
}

String NodeService::describeSleep() const {
    String text = String("sleep ") + sleepModeName(sleepMode_) +
                  (sleepModeStored_ ? " (stored)" : " (default)") + ", fraction ";
    if (sleepFraction_ < 0) return text + "default";
    return text + sleepFraction_ + " % (stored)";
}

void NodeService::sleepInto(Fields& out) const {
    enum Field : size_t { MODE, SOURCE, FRACTION, DEFAULT_FRACTION };
    out.setU(MODE, static_cast<uint32_t>(sleepMode_));
    out.setU(SOURCE, sleepModeStored_ ? 1 : 0);
    if (sleepFraction_ < 0) {
        out.setU(DEFAULT_FRACTION, 0);
    } else {
        out.setU(FRACTION, static_cast<uint32_t>(sleepFraction_));
    }
}

TypedResult NodeService::setSleepMode(uint32_t mode) {
    SleepMode parsed;
    if (mode > UINT8_MAX || !sleepModeFromValue(static_cast<uint8_t>(mode), parsed)) {
        return CommandStatus::ARG;
    }
    return storeAndRestart(NVS_KEY_SLEEP_MODE, static_cast<uint8_t>(parsed));
}

TypedResult NodeService::setSleepFraction(uint32_t percent) {
    if (percent > MAX_SLEEP_FRACTION_PERCENT) return CommandStatus::ARG;
    return storeAndRestart(NVS_KEY_SLEEP_FRACTION, static_cast<uint8_t>(percent));
}

TypedResult NodeService::storeAndRestart(const char* key, uint8_t value) {
    if (OtaBootGuard::getInstance().isPendingVerify()) return TypedResult(NODE_ERR_VERIFYING);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return TypedResult(NODE_ERR_STORE, static_cast<uint32_t>(err));
    err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return TypedResult(NODE_ERR_STORE, static_cast<uint32_t>(err));

    if (!DeferredRestart::getInstance().request()) return TypedResult(NODE_ERR_RESTART);
    ESP_LOGI(NODE_TAG, "Setting %s stored as %u, restarting", key, static_cast<unsigned>(value));
    return TypedResult();
}
