#include "otaWifiPull.h"

#include <Arduino.h>

#include <memory>
#include <new>
#include <vector>

#include "bootloader_common.h"
#include "esp_attr.h"
#include "esp_flash_partitions.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"

#include "config.h"
#include "devices/initDevices.h"
#include "espOtaFlash.h"
#include "loramesh/loraMeshService.h"
#include "otaBootGuard.h"
#include "otaKeys.h"
#include "otaSha256.h"
#include "otaSignature.h"
#include "otaStreamWriter.h"
#include "otaTransfer.h"

static const char* OP_TAG = "OtaPull";

static const char* MANIFEST_FILE = "manifest.bin";
static const char* IMAGE_FILE = "firmware.bin";
static constexpr int HTTP_TIMEOUT_MS = 10000;
static constexpr size_t HTTP_CHUNK_SIZE = 4096;

namespace {

/** Body of an HTTP GET, streamed in chunks. */
class HttpSource : public IImageSource {
public:
    explicit HttpSource(std::string url) : url_(std::move(url)) {}

    bool fetch(const Sink& sink) override {
        esp_http_client_config_t config = {};
        config.url = url_.c_str();
        config.timeout_ms = HTTP_TIMEOUT_MS;
        config.buffer_size = HTTP_CHUNK_SIZE;
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == nullptr) return false;
        bool ok = transfer(client, sink);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ok;
    }

private:
    bool transfer(esp_http_client_handle_t client, const Sink& sink) {
        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGW(OP_TAG, "GET %s: %s", url_.c_str(), esp_err_to_name(err));
            return false;
        }
        int length = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGW(OP_TAG, "GET %s: HTTP %d", url_.c_str(), status);
            return false;
        }
        std::unique_ptr<char[]> buffer(new (std::nothrow) char[HTTP_CHUNK_SIZE]);
        if (!buffer) return false;
        for (;;) {
            int read = esp_http_client_read(client, buffer.get(), HTTP_CHUNK_SIZE);
            if (read < 0) {
                ESP_LOGW(OP_TAG, "GET %s: read error", url_.c_str());
                return false;
            }
            if (read == 0) break;
            if (!sink(reinterpret_cast<const uint8_t*>(buffer.get()), read)) return true;
        }
        if (!esp_http_client_is_complete_data_received(client)) {
            ESP_LOGW(OP_TAG, "GET %s: body cut (Content-Length %d)", url_.c_str(), length);
            return false;
        }
        return true;
    }

    std::string url_;
};

/** Image whose download or check failed. Survives esp_restart(), not a power cut. */
struct FailedImage {
    uint32_t magic;
    ShaPrefix sha;
};

constexpr uint32_t FAILED_IMAGE_MAGIC = 0x4C4D4631;
RTC_NOINIT_ATTR FailedImage failedImage;

bool loadFailedImage(ShaPrefix& out) {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT) failedImage.magic = 0;
    if (failedImage.magic != FAILED_IMAGE_MAGIC) return false;
    out = failedImage.sha;
    return true;
}

}  // namespace

void OtaWifiPull::run() {
    std::string base = serverUrl();
    if (base.empty()) return;

    OtaManifest manifest;
    if (!fetchManifest(base + MANIFEST_FILE, manifest)) return;
    ESP_LOGI(OP_TAG, "Server offers %s for %s, %u B, key %u", manifest.versionString.c_str(),
             manifest.boardEnv.c_str(), manifest.imageSize, manifest.keyId);

    PolicyDecision decision = OtaPolicy::decide(manifest, nodeState());
    if (decision != PolicyDecision::INSTALL) {
        ESP_LOGI(OP_TAG, "Not installing it (decision %u)", static_cast<unsigned>(decision));
        return;
    }
    install(manifest, base + IMAGE_FILE);
}

std::string OtaWifiPull::serverUrl() {
    std::string url = OTA_SERVER_URL;
    if (!url.empty() && url.back() != '/') url += '/';
    return url;
}

bool OtaWifiPull::fetchManifest(const std::string& url, OtaManifest& out) {
    std::vector<uint8_t> body;
    bool tooLong = false;
    HttpSource source(url);
    bool ok = source.fetch([&](const uint8_t* data, size_t size) {
        tooLong = body.size() + size > OtaManifestCodec::SIGNED_SIZE;
        if (!tooLong) body.insert(body.end(), data, data + size);
        return !tooLong;
    });
    if (!ok) return false;
    if (tooLong || body.size() != OtaManifestCodec::SIGNED_SIZE) {
        ESP_LOGW(OP_TAG, "Manifest has the wrong size");
        return false;
    }

    ManifestCheck check = verifySignedManifest(body.data(), body.size(), otaTrustedKeys(),
                                               otaAcceptTestKeys(), verifyP256Sha256, out);
    if (check != ManifestCheck::OK) {
        ESP_LOGE(OP_TAG, "Manifest rejected (check %u)", static_cast<unsigned>(check));
        return false;
    }
    return true;
}

NodeState OtaWifiPull::nodeState() {
    NodeState state;
    state.boardEnv = BUILD_ENV_NAME;
    const char* running = esp_ota_get_app_description()->version;
    if (!OtaPolicy::parseVersion(running, state.runningVersion)) {
        ESP_LOGW(OP_TAG, "Running version %s cannot be parsed", running);
    }

    uint8_t sha[32];
    if (bootloader_common_get_sha256_of_partition(ESP_PARTITION_TABLE_OFFSET,
                                                  ESP_PARTITION_TABLE_MAX_LEN, PART_TYPE_DATA,
                                                  sha) == ESP_OK) {
        std::copy(sha, sha + state.partitionTablePrefix.size(), state.partitionTablePrefix.begin());
    }
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    state.slotSize = next != nullptr ? next->size : 0;
    state.blacklist = OtaBootGuard::getInstance().record().blacklist;
    state.hasFailedImage = loadFailedImage(state.failedImage);

    PowerState power;
    if (InitDevices::readPower(power)) {
        state.batteryMv = power.batteryMv;
        state.externalPower = power.externalPower;
    }
    state.freeHeap = esp_get_free_heap_size();
    return state;
}

void OtaWifiPull::install(const OtaManifest& manifest, const std::string& imageUrl) {
    ESP_LOGW(OP_TAG, "Installing %s, stopping the mesh", manifest.versionString.c_str());
    LoRaMeshService::getInstance().standby();

    EspOtaFlash flash;
    MbedSha256 sha;
    OtaStreamWriter writer(flash, sha);
    HttpSource source(imageUrl);
    int64_t start = esp_timer_get_time();
    TransferResult result = OtaTransfer::run(source, writer, manifest);
    uint32_t elapsedMs = static_cast<uint32_t>((esp_timer_get_time() - start) / 1000);
    ESP_LOGW(OP_TAG, "Image %s after %u attempt(s): %u B in %u ms (flash %u ms)",
             updateStatusName(result.status), result.attempts, result.bytes, elapsedMs,
             flash.busyMs());

    if (result.status == UpdateStatus::OK) {
        OtaBootGuard::getInstance().recordAttempt(manifest.imageShaPrefix());
        if (flash.activate()) {
            ESP_LOGW(OP_TAG, "Rebooting into %s on %s", manifest.versionString.c_str(),
                     flash.partition()->label);
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
    }

    failedImage.magic = FAILED_IMAGE_MAGIC;
    failedImage.sha = manifest.imageShaPrefix();
    ESP_LOGE(OP_TAG, "Update failed, restarting the running image");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
