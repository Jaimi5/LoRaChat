#include "otaUploadServer.h"

#include <Arduino.h>

#include <algorithm>
#include <memory>
#include <new>
#include <string>

#include "esp_ota_ops.h"
#include "esp_timer.h"

#include "espOtaFlash.h"
#include "loramesh/loraMeshService.h"
#include "otaBootGuard.h"
#include "otaBundle.h"
#include "otaInstall.h"
#include "otaSha256.h"
#include "otaStreamWriter.h"

static const char* US_TAG = "OtaUpload";
static constexpr size_t UPLOAD_CHUNK_SIZE = 4096;
static constexpr int MAX_RECV_TIMEOUTS = 3;
// Room for the ECDSA verification and the flash writer.
static constexpr size_t SERVER_STACK_SIZE = 10240;
static constexpr uint32_t REPLY_FLUSH_MS = 500;

static const char PAGE[] = R"html(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LoRaChat update</title>
<style>body{font-family:sans-serif;margin:16px;max-width:480px}progress{width:100%}
pre{white-space:pre-wrap}button{margin:8px 0}</style></head><body>
<h1>LoRaChat update</h1><p id="node">Reading node state...</p>
<input type="file" id="file" accept=".bin"><br><button id="go">Upload bundle.bin</button>
<progress id="bar" max="100" value="0"></progress><pre id="out"></pre>
<script>
const $ = id => document.getElementById(id);
fetch('/status').then(r => r.json()).then(s => {
  $('node').textContent = `Node ${s.node}: ${s.version} on ${s.partition} (${s.state})`;
});
$('go').onclick = () => {
  const f = $('file').files[0];
  if (!f) return;
  const x = new XMLHttpRequest();
  x.open('POST', '/update');
  x.upload.onprogress = e => { $('bar').value = e.loaded * 100 / e.total; };
  x.onload = () => { $('out').textContent = x.responseText; };
  x.onerror = () => { $('out').textContent = 'Upload failed: connection lost'; };
  $('out').textContent = 'Uploading ' + f.name + ' (' + f.size + ' B)...';
  x.send(f);
};
</script></body></html>
)html";

httpd_handle_t OtaUploadServer::server_ = nullptr;

bool OtaUploadServer::start() {
    if (server_ != nullptr) return true;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = SERVER_STACK_SIZE;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(US_TAG, "HTTP server start failed: %s", esp_err_to_name(err));
        server_ = nullptr;
        return false;
    }
    httpd_uri_t page = {"/", HTTP_GET, handlePage, nullptr};
    httpd_uri_t status = {"/status", HTTP_GET, handleStatus, nullptr};
    httpd_uri_t update = {"/update", HTTP_POST, handleUpdate, nullptr};
    httpd_register_uri_handler(server_, &page);
    httpd_register_uri_handler(server_, &status);
    httpd_register_uri_handler(server_, &update);
    return true;
}

void OtaUploadServer::stop() {
    if (server_ == nullptr) return;
    httpd_stop(server_);
    server_ = nullptr;
}

esp_err_t OtaUploadServer::handlePage(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, sizeof(PAGE) - 1);
}

esp_err_t OtaUploadServer::handleStatus(httpd_req_t* req) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    OtaRecord record = OtaBootGuard::getInstance().record();
    char json[192];
    snprintf(json, sizeof(json),
             "{\"node\":\"%04X\",\"version\":\"%s\",\"partition\":\"%s\",\"state\":\"%s\","
             "\"last_outcome\":%u}",
             LoRaMeshService::getInstance().getLocalAddress(),
             esp_ota_get_app_description()->version, running ? running->label : "?",
             OtaBootGuard::getInstance().isPendingVerify() ? "PENDING_VERIFY" : "VALID",
             static_cast<unsigned>(record.lastOutcome));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

esp_err_t OtaUploadServer::handleUpdate(httpd_req_t* req) {
    EspOtaFlash flash;
    MbedSha256 sha;
    OtaStreamWriter writer(flash, sha);
    std::string reason;
    bool meshStopped = false;
    OtaBundleReader reader(
        writer,
        [&reason](const uint8_t* data, size_t size, OtaManifest& out) {
            Acceptance acceptance = OtaInstall::accept(data, size, out);
            reason = acceptance.reason;
            return acceptance.install();
        },
        [&meshStopped](const OtaManifest& manifest) {
            meshStopped = true;
            OtaInstall::stopMesh(manifest);
        });

    std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[UPLOAD_CHUNK_SIZE]);
    if (!buffer) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

    size_t remaining = req->content_len;
    ESP_LOGI(US_TAG, "Upload of %u B", static_cast<unsigned>(remaining));
    int64_t start = esp_timer_get_time();
    reader.begin(static_cast<uint32_t>(remaining));
    UpdateStatus status = UpdateStatus::OK;
    int timeouts = 0;
    while (remaining > 0 && status == UpdateStatus::OK) {
        int received = httpd_req_recv(req, reinterpret_cast<char*>(buffer.get()),
                                      std::min(remaining, UPLOAD_CHUNK_SIZE));
        if (received == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= MAX_RECV_TIMEOUTS) continue;
        if (received <= 0) break;
        timeouts = 0;
        remaining -= received;
        status = reader.push(buffer.get(), received);
    }
    if (status == UpdateStatus::OK) {
        status = remaining == 0 ? reader.finish() : UpdateStatus::INCOMPLETE;
        if (status == UpdateStatus::INCOMPLETE) reader.abort();
    }
    uint32_t elapsedMs = static_cast<uint32_t>((esp_timer_get_time() - start) / 1000);
    ESP_LOGW(US_TAG, "Upload %s in %u ms (flash %u ms)%s%s", updateStatusName(status), elapsedMs,
             flash.busyMs(), reason.empty() ? "" : ", manifest: ", reason.c_str());

    std::string reply;
    if (status == UpdateStatus::OK) {
        reply = "OK: " + reader.manifest().versionString + " written in " +
                std::to_string(elapsedMs / 1000) +
                " s. The node reboots and checks the new image; reload this page in a few "
                "minutes to see the result.";
        httpd_resp_sendstr(req, reply.c_str());
        vTaskDelay(pdMS_TO_TICKS(REPLY_FLUSH_MS));
        OtaInstall::rebootInto(flash, reader.manifest());
    }

    reply = std::string("ERROR ") + updateStatusName(status);
    if (status == UpdateStatus::MANIFEST_REJECTED) reply += ": " + reason;
    if (meshStopped) reply += ". The node restarts its current image.";
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, reply.c_str());
    if (meshStopped) {
        vTaskDelay(pdMS_TO_TICKS(REPLY_FLUSH_MS));
        OtaInstall::restartAfterFailure(reader.manifest());
    }
    return ESP_OK;
}
