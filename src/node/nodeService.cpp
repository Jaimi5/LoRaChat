#include "nodeService.h"

#include "config.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "ota/otaBootGuard.h"

static const char* NODE_TAG = "NodeService";
static const char* NVS_NAMESPACE = "lmnode";
static const char* NVS_KEY_ROLE = "role";
static constexpr uint64_t RESTART_DELAY_US = 1000 * 1000;

void NodeService::init(uint16_t address) {
    nvs_handle_t handle;
    uint8_t value = 0;
    stored_ = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK &&
              nvs_get_u8(handle, NVS_KEY_ROLE, &value) == ESP_OK && nodeRoleFromValue(value, role_);
    if (stored_) nvs_close(handle);
    if (!stored_) role_ = defaultNodeRole(address, LORA_MANAGER_ID);
    ESP_LOGI(NODE_TAG, "Node role: %s", describeRole().c_str());
}

String NodeService::describeRole() const {
    return String(nodeRoleName(role_)) + (stored_ ? " (stored)" : " (default)");
}

String NodeService::setRole(const String& text) {
    NodeRole role;
    String name = text;
    name.trim();
    if (!parseNodeRole(name.c_str(), role)) return "Usage: /role.set gateway|sensor";
    if (OtaBootGuard::getInstance().isPendingVerify()) {
        return "The image is still being verified; try again when it is valid";
    }

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return "NVS not available";
    esp_err_t err = nvs_set_u8(handle, NVS_KEY_ROLE, static_cast<uint8_t>(role));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return String("Could not store the role: ") + esp_err_to_name(err);

    esp_timer_create_args_t args = {};
    args.callback = [](void*) { esp_restart(); };
    args.name = "roleRestart";
    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&args, &timer) == ESP_OK) esp_timer_start_once(timer, RESTART_DELAY_US);
    ESP_LOGI(NODE_TAG, "Role set to %s, restarting", nodeRoleName(role));
    return String("Role set to ") + nodeRoleName(role) + ", restarting";
}
