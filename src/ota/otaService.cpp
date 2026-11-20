#include "otaService.h"

#include <cstring>
#include <vector>

#include "cmdSeal.h"
#include "cmdText.h"
#include "commands/commandRouter.h"
#include "config.h"
#include "esp_ota_ops.h"
#include "loramesh/loraMeshService.h"
#include "maintWifi.h"
#include "node/deferredRestart.h"
#include "node/nodeService.h"
#include "nvs.h"
#include "otaBootGuard.h"
#include "wifi/wifiServerService.h"

static const char* OS_TAG = "OtaService";
static const char* NVS_NAMESPACE = "lmsec";
static const char* NVS_KEY_DEPLOYMENT = "cmdkey";

static const char* NVS_OTA_NAMESPACE = "lmota";
static const char* NVS_KEY_SERVER = "srv";
static constexpr size_t MAX_SERVER_URL = 128;
static const uint8_t SEAL_LABEL[] = {'L', 'M', 'W', '1'};

/** Bytes of the bootloader hash that /version shows. */
static constexpr size_t BOOTLOADER_HASH_BYTES = 3;

static constexpr uint32_t MAX_PULL_WINDOW_S = MaintWindowPlan::MAX_PULL_S;
static constexpr uint32_t MAX_AP_WINDOW_S = MaintWindowPlan::MAX_AP_S;

bool OtaService::deploymentKey(DeploymentKey& out) const {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t size = out.size();
    esp_err_t err = nvs_get_blob(handle, NVS_KEY_DEPLOYMENT, out.data(), &size);
    nvs_close(handle);
    return err == ESP_OK && size == out.size();
}

TypedResult OtaService::keyInto(Fields& out) const {
    enum Field : size_t { NO_KEY, FINGERPRINT };
    DeploymentKey key;
    std::vector<uint8_t> fingerprint;
    if (!deploymentKey(key) || !fromHex(keyFingerprint(key), fingerprint)) {
        out.setU(NO_KEY, 0);
        return TypedResult();
    }
    out.setBytes(FINGERPRINT, fingerprint.data(), fingerprint.size());
    return TypedResult();
}

CommandReply OtaService::setKey(const String& text) {
    DeploymentKey key;
    if (!parseDeploymentKey(text.c_str(), key)) return CommandStatus::ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return CommandReply(OTA_ERR_FAILED, esp_err_to_name(err));
    err = nvs_set_blob(handle, NVS_KEY_DEPLOYMENT, key.data(), key.size());
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return CommandReply(OTA_ERR_FAILED, esp_err_to_name(err));

    std::string fingerprint = keyFingerprint(key);
    ESP_LOGI(OS_TAG, "Deployment key stored, fingerprint %s", fingerprint.c_str());
    return String("Deployment key stored, fingerprint ") + fingerprint.c_str();
}

TypedResult OtaService::openWindow(uint32_t seconds, bool accessPoint, Fields& out) {
    const uint32_t maxSeconds = accessPoint ? MAX_AP_WINDOW_S : MAX_PULL_WINDOW_S;
    if (seconds == 0 || seconds > maxSeconds) return CommandStatus::ARG;
    MaintenanceWifi& wifi = MaintenanceWifi::getInstance();
    return accessPoint ? wifi.openApWindow(seconds, out) : wifi.openPullWindow(seconds, out);
}

TypedResult OtaService::closeWindow(Fields& out) {
    enum Field : size_t { NONE_OPEN, CLOSING };
    out.setU(MaintenanceWifi::getInstance().closeWindow() ? CLOSING : NONE_OPEN, 0);
    return TypedResult();
}

TypedResult OtaService::versionInto(Fields& out) const {
    enum Field : size_t { VERSION, SLOT, STATE, BOOTLOADER, ENV, COMMIT, DIRTY };
    static const char* const SLOTS[] = {"app0", "app1"};
    static const char* const ENVS[] = {"tbeam", "tbeam-debug"};
    const std::string text = esp_ota_get_app_description()->version;
    uint8_t version[FIELD_VERSION_BYTES] = {};
    parseVersionPrefix(text, version);
    out.setVersion(VERSION, version[0], version[1], version[2]);
    uint8_t commit[VERSION_COMMIT_BYTES] = {};
    bool dirty = false;
    parseVersionBuild(text, commit, dirty);
    out.setBytes(COMMIT, commit, sizeof(commit));
    if (dirty) out.setU(DIRTY, 0);
    const esp_partition_t* running = esp_ota_get_running_partition();
    for (uint32_t i = 0; i < sizeof(SLOTS) / sizeof(SLOTS[0]); i++) {
        if (running != nullptr && strcmp(running->label, SLOTS[i]) == 0) out.setU(SLOT, i);
    }
    out.setU(STATE, OtaBootGuard::getInstance().isPendingVerify() ? 1 : 0);
    std::vector<uint8_t> hash;
    if (!fromHex(OtaBootGuard::bootloaderHash(), hash)) hash.assign(BOOTLOADER_HASH_BYTES, 0);
    out.setBytes(BOOTLOADER, hash.data(), hash.size());
    for (uint32_t i = 0; i < sizeof(ENVS) / sizeof(ENVS[0]); i++) {
        if (strcmp(BUILD_ENV_NAME, ENVS[i]) == 0) out.setU(ENV, i);
    }
    return TypedResult();
}

TypedResult OtaService::statusInto(Fields& out) const {
    enum Field : size_t { LAST, PENDING, REASON, UNEXPLAINED, BLACKLISTED };
    const OtaRecord record = OtaBootGuard::getInstance().record();
    if (record.lastOutcome <= OtaOutcome::BL_NO_ROLLBACK) {
        out.setU(LAST, static_cast<uint32_t>(record.lastOutcome));
    }
    out.setU(PENDING, record.pending ? 1 : 0);
    out.setU(REASON, static_cast<uint32_t>(record.failReason));
    out.setU(UNEXPLAINED, record.unexplainedRollbacks);
    out.setU(BLACKLISTED, static_cast<uint32_t>(record.blacklist.size()));
    return TypedResult();
}

TypedResult OtaService::reboot() {
    if (!DeferredRestart::getInstance().request()) return TypedResult(OTA_ERR_FAILED);
    ESP_LOGW(OS_TAG, "Reboot requested");
    return TypedResult();
}

std::string OtaService::serverUrl() const {
    nvs_handle_t handle;
    char url[MAX_SERVER_URL + 1] = {};
    size_t size = sizeof(url);
    if (nvs_open(NVS_OTA_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        esp_err_t err = nvs_get_str(handle, NVS_KEY_SERVER, url, &size);
        nvs_close(handle);
        if (err == ESP_OK && url[0] != '\0') return url;
    }
    return OTA_SERVER_URL;
}

CommandReply OtaService::setServer(const String& args) {
    String url = args;
    url.trim();
    if (url.isEmpty()) return String("OTA server: ") + serverUrl().c_str();
    bool reset = url == "default";
    if (!reset && (!url.startsWith("http://") || url.length() > MAX_SERVER_URL)) {
        return CommandStatus::ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_OTA_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return CommandReply(OTA_ERR_FAILED, esp_err_to_name(err));
    err = reset ? nvs_erase_key(handle, NVS_KEY_SERVER)
                : nvs_set_str(handle, NVS_KEY_SERVER, url.c_str());
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return CommandReply(OTA_ERR_FAILED, esp_err_to_name(err));
    return String("OTA server: ") + serverUrl().c_str();
}

TypedResult OtaService::setWifiSealed(const std::vector<uint8_t>& sealed, Fields& out) {
    uint32_t counter = 0;
    DeploymentKey key;
    if (!CommandRouter::getInstance().runningCounter(counter) || !deploymentKey(key)) {
        return CommandStatus::ARG;
    }
    Sha256 sealKey = hmacSha256(key.data(), key.size(), SEAL_LABEL, sizeof(SEAL_LABEL));
    KeyedHmac sealHmac = [&sealKey](const uint8_t* data, size_t size) {
        return hmacSha256(sealKey.data(), sealKey.size(), data, size);
    };
    uint16_t local = LoRaMeshService::getInstance().getLocalAddress();
    std::vector<uint8_t> plain = sealCommandArgs(sealHmac, local, counter, sealed);
    std::string ssid;
    std::string password;
    if (!decodeWifiCredentials(plain, ssid, password)) return TypedResult(WIFI_ERR_INVALID);
    return storeWifi(ssid, password, out);
}

TypedResult OtaService::setWifiPlain(const String& args, Fields& out) {
    std::string text = args.c_str();
    size_t space = text.find(' ');
    std::string ssid = text.substr(0, space);
    std::string password = space == std::string::npos ? "" : text.substr(space + 1);
    if (!decodeWifiCredentials(encodeWifiCredentials(ssid, password), ssid, password)) {
        return CommandStatus::ARG;
    }
    return storeWifi(ssid, password, out);
}

TypedResult OtaService::storeWifi(const std::string& ssid, const std::string& password,
                                  Fields& out) {
    const bool gateway = NodeService::getInstance().isGateway();
    TypedResult stored =
        gateway ? WiFiServerService::getInstance().storeCredentials(ssid.c_str(), password.c_str())
                : MaintenanceWifi::getInstance().storeCredentials(ssid, password);
    if (!stored.ok()) return stored;
    if (gateway) out.setU(MAINT_WIFI_RECONNECTING, 0);
    out.setText(MAINT_WIFI_SSID, ssid);
    return TypedResult();
}
