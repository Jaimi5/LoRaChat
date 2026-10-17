#include "otaWifiPull.h"

#include <Arduino.h>

#include <memory>
#include <new>
#include <vector>

#include "esp_http_client.h"
#include "esp_timer.h"

#include "config.h"
#include "espOtaFlash.h"
#include "otaInstall.h"
#include "otaReporter.h"
#include "otaService.h"
#include "otaSha256.h"
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

}  // namespace

void OtaWifiPull::run() {
    std::string base = serverUrl();
    if (base.empty()) return;
    OtaReporter::sendPendingVerdict();

    OtaManifest manifest;
    Acceptance acceptance;
    if (!fetchManifest(base + MANIFEST_FILE, manifest, acceptance)) return;
    if (!acceptance.install()) {
        ReportDetails details;
        if (acceptance.check == ManifestCheck::OK) {
            details.decision = static_cast<int>(acceptance.decision);
            OtaReporter::send(ReportEvent::NOOP, details);
        } else {
            details.status = static_cast<int>(UpdateStatus::MANIFEST_REJECTED);
            OtaReporter::send(ReportEvent::FAILED, details);
        }
        return;
    }
    install(manifest, base + IMAGE_FILE);
}

std::string OtaWifiPull::serverUrl() {
    std::string url = OtaService::getInstance().serverUrl();
    if (!url.empty() && url.back() != '/') url += '/';
    return url;
}

bool OtaWifiPull::fetchManifest(const std::string& url, OtaManifest& out,
                                Acceptance& acceptance) {
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

    acceptance = OtaInstall::accept(body.data(), body.size(), out);
    return true;
}

void OtaWifiPull::install(const OtaManifest& manifest, const std::string& imageUrl) {
    OtaInstall::stopMesh(manifest);

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

    ReportDetails details;
    details.status = static_cast<int>(result.status);
    details.sha = OtaReporter::shaHex(manifest.imageShaPrefix());
    details.download = true;
    details.downloadMs = elapsedMs;
    details.bytes = result.bytes;
    if (result.status == UpdateStatus::OK) {
        OtaReporter::send(ReportEvent::WRITTEN, details);
        OtaInstall::rebootInto(flash, manifest);
    }
    OtaReporter::send(ReportEvent::FAILED, details);
    OtaInstall::restartAfterFailure(manifest);
}
