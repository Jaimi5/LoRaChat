#include "otaService.h"

#include <cstdlib>

#include "maintWifi.h"
#include "nvs.h"

static const char* OS_TAG = "OtaService";
static const char* NVS_NAMESPACE = "lmsec";
static const char* NVS_KEY_DEPLOYMENT = "cmdkey";

static constexpr uint32_t MAX_PULL_WINDOW_S = 7200;
static constexpr uint32_t MAX_AP_WINDOW_S = 1800;

bool OtaService::deploymentKey(DeploymentKey& out) const {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t size = out.size();
    esp_err_t err = nvs_get_blob(handle, NVS_KEY_DEPLOYMENT, out.data(), &size);
    nvs_close(handle);
    return err == ESP_OK && size == out.size();
}

String OtaService::describeKey() const {
    DeploymentKey key;
    if (!deploymentKey(key)) return "No deployment key";
    return String("Deployment key stored, fingerprint ") + keyFingerprint(key).c_str();
}

String OtaService::setKey(const String& text) {
    DeploymentKey key;
    if (!parseDeploymentKey(text.c_str(), key)) return "Usage: /key.set <64 hex digits>";

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return "NVS not available";
    esp_err_t err = nvs_set_blob(handle, NVS_KEY_DEPLOYMENT, key.data(), key.size());
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return String("Could not store the key: ") + esp_err_to_name(err);

    std::string fingerprint = keyFingerprint(key);
    ESP_LOGI(OS_TAG, "Deployment key stored, fingerprint %s", fingerprint.c_str());
    return String("Deployment key stored, fingerprint ") + fingerprint.c_str();
}

String OtaService::openWindow(const String& args) {
    String text = args;
    text.trim();
    int space = text.indexOf(' ');
    String seconds = space < 0 ? text : text.substring(0, space);
    String kind = space < 0 ? "" : text.substring(space + 1);
    kind.trim();

    char* end = nullptr;
    unsigned long duration = strtoul(seconds.c_str(), &end, 10);
    bool accessPoint = kind == "ap";
    uint32_t maxSeconds = accessPoint ? MAX_AP_WINDOW_S : MAX_PULL_WINDOW_S;
    if (seconds.isEmpty() || *end != '\0' || duration == 0 || duration > maxSeconds ||
        (!kind.isEmpty() && !accessPoint)) {
        return String("Usage: /maint.open <seconds> [ap], up to ") + MAX_PULL_WINDOW_S +
               " s, or " + MAX_AP_WINDOW_S + " s with ap";
    }

    MaintenanceWifi& wifi = MaintenanceWifi::getInstance();
    std::string reply = accessPoint ? wifi.openApWindow(duration * 1000)
                                    : wifi.openPullWindow(duration * 1000);
    return reply.c_str();
}

String OtaService::closeWindow() {
    return MaintenanceWifi::getInstance().closeWindow().c_str();
}
