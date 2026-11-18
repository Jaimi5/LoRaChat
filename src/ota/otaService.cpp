#include "otaService.h"

#include <cstdlib>
#include <vector>

#include "cmdSeal.h"
#include "cmdText.h"
#include "commands/commandRouter.h"
#include "config.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "loramesh/loraMeshService.h"
#include "maintWifi.h"
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
static constexpr uint64_t REBOOT_DELAY_US = 1000ULL * 1000;
static const uint8_t SEAL_LABEL[] = {'L', 'M', 'W', '1'};

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

String OtaService::describeKey() const {
    DeploymentKey key;
    if (!deploymentKey(key)) return "No deployment key";
    return String("Deployment key stored, fingerprint ") + keyFingerprint(key).c_str();
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

CommandReply OtaService::openWindow(const String& args) {
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
        return CommandStatus::ARG;
    }

    MaintenanceWifi& wifi = MaintenanceWifi::getInstance();
    return accessPoint ? wifi.openApWindow(duration) : wifi.openPullWindow(duration);
}

String OtaService::closeWindow() {
    return MaintenanceWifi::getInstance().closeWindow().c_str();
}

String OtaService::version() const {
    const esp_partition_t* running = esp_ota_get_running_partition();
    char text[128];
    snprintf(text, sizeof(text), "%s %s %s bl=%s env=%s node=%04X",
             esp_ota_get_app_description()->version, running ? running->label : "?",
             OtaBootGuard::getInstance().isPendingVerify() ? "PENDING_VERIFY" : "VALID",
             OtaBootGuard::bootloaderHash().c_str(), BUILD_ENV_NAME,
             LoRaMeshService::getInstance().getLocalAddress());
    return text;
}

String OtaService::otaStatus() const {
    static const char* OUTCOMES[] = {"NONE", "VALID", "ROLLED_BACK", "BL_NO_ROLLBACK"};
    OtaRecord record = OtaBootGuard::getInstance().record();
    size_t outcome = static_cast<size_t>(record.lastOutcome);
    char text[128];
    snprintf(text, sizeof(text), "last=%s pending=%s reason=%u unexplained=%u blacklisted=%u",
             outcome < sizeof(OUTCOMES) / sizeof(OUTCOMES[0]) ? OUTCOMES[outcome] : "?",
             record.pending ? "yes" : "no", static_cast<unsigned>(record.failReason),
             record.unexplainedRollbacks, static_cast<unsigned>(record.blacklist.size()));
    return text;
}

CommandReply OtaService::reboot() {
    esp_timer_create_args_t args = {};
    args.callback = [](void*) { esp_restart(); };
    args.name = "reboot";
    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&args, &timer) != ESP_OK ||
        esp_timer_start_once(timer, REBOOT_DELAY_US) != ESP_OK) {
        return CommandReply(OTA_ERR_FAILED, "");
    }
    MaintenanceWifi::getInstance().saveClock();
    ESP_LOGW(OS_TAG, "Reboot requested");
    return "Rebooting";
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

CommandReply OtaService::setWifi(const String& args) {
    std::string ssid;
    std::string password;
    uint32_t counter = 0;
    if (CommandRouter::getInstance().runningCounter(counter)) {
        std::vector<uint8_t> sealed;
        DeploymentKey key;
        if (!fromHex(args.c_str(), sealed) || !deploymentKey(key)) {
            return CommandStatus::ARG;
        }
        Sha256 sealKey = hmacSha256(key.data(), key.size(), SEAL_LABEL, sizeof(SEAL_LABEL));
        KeyedHmac sealHmac = [&sealKey](const uint8_t* data, size_t size) {
            return hmacSha256(sealKey.data(), sealKey.size(), data, size);
        };
        uint16_t local = LoRaMeshService::getInstance().getLocalAddress();
        std::vector<uint8_t> plain = sealCommandArgs(sealHmac, local, counter, sealed);
        if (!decodeWifiCredentials(plain, ssid, password)) {
            return CommandReply(WIFI_ERR_INVALID, "");
        }
    } else {
        std::string text = args.c_str();
        size_t space = text.find(' ');
        ssid = text.substr(0, space);
        password = space == std::string::npos ? "" : text.substr(space + 1);
        if (!decodeWifiCredentials(encodeWifiCredentials(ssid, password), ssid, password)) {
            return CommandStatus::ARG;
        }
    }

    if (NodeService::getInstance().isGateway()) {
        return WiFiServerService::getInstance().storeCredentials(ssid.c_str(), password.c_str());
    }
    return MaintenanceWifi::getInstance().storeCredentials(ssid, password);
}
