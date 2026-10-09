#include "otaReporter.h"

#include <Arduino.h>

#include <vector>

#include "config.h"
#include "deploymentKey.h"
#include "devices/initDevices.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "loramesh/loraMeshService.h"
#include "node/nodeService.h"
#include "nvs.h"
#include "otaBootGuard.h"
#include "otaService.h"
#include "wifi/wifiServerService.h"

static const char* OR_TAG = "OtaReport";
static const char* NVS_NAMESPACE = "lmota";
static const char* NVS_KEY_REPORTED = "rptd";
static const char* TAG_HEADER = "X-LM-Tag";
static constexpr int HTTP_TIMEOUT_MS = 5000;
static constexpr uint32_t CONNECT_POLL_MS = 5000;
static constexpr int CONNECT_POLLS = 360;
// HTTP client and HMAC.
static constexpr uint32_t VERDICT_TASK_STACK = 6144;

namespace {

/** NVS blob of the last verdict sent: attempt SHA prefix and outcome. */
std::vector<uint8_t> verdictKey(const OtaRecord& record) {
    std::vector<uint8_t> key(record.attemptSha.begin(), record.attemptSha.end());
    key.push_back(static_cast<uint8_t>(record.lastOutcome));
    return key;
}

}  // namespace

bool OtaReporter::send(ReportEvent event, const ReportDetails& details) {
    char node[5];
    snprintf(node, sizeof(node), "%04X", LoRaMeshService::getInstance().getLocalAddress());
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macText[18];
    snprintf(macText, sizeof(macText), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    const esp_partition_t* running = esp_ota_get_running_partition();
    OtaBootGuard& guard = OtaBootGuard::getInstance();

    ReportBuilder report;
    report.addText("node", node)
        .addText("mac", macText)
        .addText("env", BUILD_ENV_NAME)
        .addText("role", NodeService::getInstance().isGateway() ? "gateway" : "sensor")
        .addText("ver", esp_ota_get_app_description()->version)
        .addText("part", running ? running->label : "?")
        .addText("state", guard.isPendingVerify() ? "PENDING_VERIFY" : "VALID")
        .addNumber("boot", guard.bootCount())
        .addText("rr", OtaBootGuard::resetReasonName())
        .addText("bl", OtaBootGuard::bootloaderHash())
        .addText("event", reportEventName(event));
    if (!details.lastInvalid.empty()) report.addText("last_invalid", details.lastInvalid);
    if (details.status >= 0) report.addNumber("status", details.status);
    if (details.decision >= 0) report.addNumber("decision", details.decision);
    if (details.reason >= 0) report.addNumber("reason", details.reason);
    if (!details.sha.empty()) report.addText("sha", details.sha);
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) report.addNumber("rssi", ap.rssi);
    if (details.download) {
        report.addNumber("dl_ms", details.downloadMs).addNumber("bytes", details.bytes);
    }
    report.addNumber("heap_free", esp_get_free_heap_size())
        .addNumber("heap_min", esp_get_minimum_free_heap_size());
    PowerState power;
    if (InitDevices::readPower(power)) {
        report.addNumber("batt_mv", power.batteryMv).addFlag("vbus", power.externalPower);
    }
    report.addNumber("uptime_s", esp_timer_get_time() / 1000000);

    std::string body = report.body();
    if (body.empty()) {
        ESP_LOGE(OR_TAG, "Report %s could not be built", reportEventName(event));
        return false;
    }
    int status = post(body);
    ESP_LOGI(OR_TAG, "Report %s: HTTP %d", reportEventName(event), status);
    return status >= 200 && status < 300;
}

void OtaReporter::sendPendingVerdict() {
    OtaRecord record = OtaBootGuard::getInstance().record();
    if (record.lastOutcome == OtaOutcome::NONE) return;
    std::vector<uint8_t> key = verdictKey(record);

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    std::vector<uint8_t> sent(key.size());
    size_t size = sent.size();
    bool alreadySent =
        nvs_get_blob(handle, NVS_KEY_REPORTED, sent.data(), &size) == ESP_OK && sent == key;
    if (!alreadySent) {
        ReportDetails details;
        details.sha = shaHex(record.attemptSha);
        ReportEvent event = ReportEvent::VALID;
        if (record.lastOutcome == OtaOutcome::ROLLED_BACK) {
            event = ReportEvent::ROLLBACK;
            details.reason = static_cast<int>(record.failReason);
            const esp_partition_t* invalid = esp_ota_get_last_invalid_partition();
            if (invalid != nullptr) details.lastInvalid = invalid->label;
        }
        if (send(event, details) &&
            nvs_set_blob(handle, NVS_KEY_REPORTED, key.data(), key.size()) == ESP_OK) {
            nvs_commit(handle);
        }
    }
    nvs_close(handle);
}

std::string OtaReporter::shaHex(const ShaPrefix& sha) {
    char text[2 * sizeof(ShaPrefix) + 1];
    for (size_t i = 0; i < sha.size(); i++) snprintf(text + 2 * i, 3, "%02x", sha[i]);
    return text;
}

void OtaReporter::sendVerdictWhenConnected() {
    auto task = [](void*) {
        // The verdict of a new image is known once the boot guard has decided.
        WiFiServerService& wifi = WiFiServerService::getInstance();
        OtaBootGuard& guard = OtaBootGuard::getInstance();
        for (int i = 0; i < CONNECT_POLLS && (guard.isPendingVerify() || !wifi.hasIp()); i++) {
            vTaskDelay(pdMS_TO_TICKS(CONNECT_POLL_MS));
        }
        if (!guard.isPendingVerify() && wifi.hasIp()) sendPendingVerdict();
        vTaskDelete(nullptr);
    };
    if (xTaskCreate(task, "OtaVerdict", VERDICT_TASK_STACK, nullptr, 1, nullptr) != pdPASS) {
        ESP_LOGE(OR_TAG, "Verdict report task creation failed");
    }
}

int OtaReporter::post(const std::string& body) {
    DeploymentKey key;
    if (!OtaService::getInstance().deploymentKey(key)) {
        ESP_LOGW(OR_TAG, "No deployment key, report not sent");
        return -1;
    }
    std::string tag = reportTag(reportKey(key), body);
    std::string url = reportUrl();

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) return -1;
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, TAG_HEADER, tag.c_str());
    esp_http_client_set_post_field(client, body.data(), body.size());
    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    if (err != ESP_OK) ESP_LOGW(OR_TAG, "POST %s: %s", url.c_str(), esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return status;
}

std::string OtaReporter::reportUrl() {
    // The report endpoint is /report on the host of the OTA server.
    std::string url = OtaService::getInstance().serverUrl();
    size_t scheme = url.find("://");
    size_t path = url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
    return url.substr(0, path) + "/report";
}
