#include "otaService.h"

#ifdef OTA_ENABLED

#include "esp_log.h"
#include "esp_crc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "mbedtls/base64.h"
#include "loramesh/loraMeshService.h"
#include "message/messageManager.h"

static const char* OTA_TAG = "OTAService";

// -- CRC-16/CCITT helper ----------------------------------------------------
static uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

// -- init -------------------------------------------------------------------
void OTAService::init() {
    ESP_LOGI(OTA_TAG, "OTA service initializing");

    esp_vfs_spiffs_conf_t conf = {
        .base_path       = "/spiffs",
        .partition_label = NULL,
        .max_files       = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(OTA_TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(OTA_TAG, "SPIFFS mounted");
    }

    xTaskCreatePinnedToCore(watchdogTask, "OTAWatchdog", 2048, NULL, 1,
                            &watchdog_task_handle_, 0);
    ESP_LOGI(OTA_TAG, "OTA service ready");
}

// -- watchdog ---------------------------------------------------------------
void OTAService::watchdogTask(void* param) {
    while (true) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        OTAService& svc = OTAService::getInstance();
        if (svc.state_ != OTA_STATE_IDLE && svc.state_ != OTA_STATE_REBOOTING) {
            uint32_t now = millis();
            if (now - svc.last_activity_ms_ > OTA_SESSION_TIMEOUT_MS) {
                ESP_LOGW(OTA_TAG, "Session timeout, resetting");
                svc.resetSession();
            }
        }
    }
}

// -- resetSession -----------------------------------------------------------
void OTAService::resetSession() {
    if (ota_handle_ != 0) {
        esp_ota_abort(ota_handle_);
        ota_handle_ = 0;
    }
    if (patch_file_ != nullptr) {
        fclose(patch_file_);
        patch_file_ = nullptr;
        remove(OTA_SPIFFS_PATCH_PATH);
    }
    if (chunk_bitmap_ != nullptr) {
        vPortFree(chunk_bitmap_);
        chunk_bitmap_ = nullptr;
    }
    state_           = OTA_STATE_IDLE;
    session_id_      = 0;
    total_chunks_    = 0;
    total_size_      = 0;
    chunks_received_ = 0;
    bytes_written_   = 0;
    addr_host_       = 0;
    update_partition_= nullptr;
    ESP_LOGI(OTA_TAG, "Session reset to IDLE");
}

// -- handleBegin ------------------------------------------------------------
void OTAService::handleBegin(const OTABeginPayload* p, uint16_t addrSrc) {
    if (state_ != OTA_STATE_IDLE) {
        ESP_LOGW(OTA_TAG, "BEGIN received but not idle, aborting old session");
        resetSession();
    }
    ESP_LOGI(OTA_TAG, "OTA BEGIN: session=%lu size=%lu chunks=%u delta=%u ver=%s",
             (unsigned long)p->session_id, (unsigned long)p->total_size,
             p->total_chunks, p->is_delta, p->fw_version);

    session_id_      = p->session_id;
    total_size_      = p->total_size;
    total_chunks_    = p->total_chunks;
    expected_crc32_  = p->fw_crc32;
    is_delta_        = p->is_delta;
    memcpy(fw_version_, p->fw_version, sizeof(fw_version_));
    chunks_received_ = 0;
    bytes_written_   = 0;
    addr_host_       = addrSrc;
    last_activity_ms_= millis();

    // Allocate chunk bitmap
    size_t bitmap_size = (total_chunks_ + 7) / 8;
    chunk_bitmap_ = (uint8_t*)pvPortMalloc(bitmap_size);
    if (!chunk_bitmap_) {
        ESP_LOGE(OTA_TAG, "Failed to allocate chunk bitmap");
        sendAck(addrSrc, session_id_, 0, 3 /*no_space*/, 0);
        return;
    }
    memset(chunk_bitmap_, 0, bitmap_size);

    if (is_delta_) {
        // Open SPIFFS file for patch storage
        patch_file_ = fopen(OTA_SPIFFS_PATCH_PATH, "wb");
        if (!patch_file_) {
            ESP_LOGE(OTA_TAG, "Failed to open patch file on SPIFFS");
            vPortFree(chunk_bitmap_);
            chunk_bitmap_ = nullptr;
            sendAck(addrSrc, session_id_, 0, 3 /*no_space*/, 0);
            return;
        }
    } else {
        // Start esp_ota for full firmware
        update_partition_ = esp_ota_get_next_update_partition(NULL);
        if (!update_partition_) {
            ESP_LOGE(OTA_TAG, "No update partition found");
            vPortFree(chunk_bitmap_);
            chunk_bitmap_ = nullptr;
            sendAck(addrSrc, session_id_, 0, 3 /*no_space*/, 0);
            return;
        }
        esp_err_t err = esp_ota_begin(update_partition_, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle_);
        if (err != ESP_OK) {
            ESP_LOGE(OTA_TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
            vPortFree(chunk_bitmap_);
            chunk_bitmap_ = nullptr;
            sendAck(addrSrc, session_id_, 0, 3 /*no_space*/, 0);
            return;
        }
    }

    state_ = OTA_STATE_RECEIVING_CHUNKS;
    sendAck(addrSrc, session_id_, 0, 0 /*ok*/, 0);
    ESP_LOGI(OTA_TAG, "OTA session started, waiting for chunks");
}

// -- handleChunk ------------------------------------------------------------
void OTAService::handleChunk(const OTAChunkPayload* p, uint16_t addrSrc) {
    if (state_ != OTA_STATE_RECEIVING_CHUNKS) {
        ESP_LOGW(OTA_TAG, "CHUNK received in state %d, ignored", state_);
        return;
    }
    if (p->session_id != session_id_) {
        ESP_LOGW(OTA_TAG, "CHUNK session mismatch");
        return;
    }
    last_activity_ms_ = millis();

    // Check if already received
    uint8_t bit = 1 << (p->seq_num % 8);
    if (chunk_bitmap_[p->seq_num / 8] & bit) {
        // Duplicate -- just re-ACK
        sendAck(addrSrc, session_id_, p->seq_num, 0, chunks_received_);
        return;
    }

    // Verify CRC16
    uint16_t computed = crc16_ccitt(p->data, p->chunk_size);
    if (computed != p->crc16) {
        ESP_LOGW(OTA_TAG, "CRC16 mismatch on seq %u", p->seq_num);
        sendAck(addrSrc, session_id_, p->seq_num, 1 /*crc_error*/, chunks_received_);
        return;
    }

    // Write data
    if (is_delta_) {
        if (fwrite(p->data, 1, p->chunk_size, patch_file_) != p->chunk_size) {
            ESP_LOGE(OTA_TAG, "SPIFFS write error at seq %u", p->seq_num);
            sendAck(addrSrc, session_id_, p->seq_num, 3 /*no_space*/, chunks_received_);
            resetSession();
            return;
        }
    } else {
        esp_err_t err = esp_ota_write(ota_handle_, p->data, p->chunk_size);
        if (err != ESP_OK) {
            ESP_LOGE(OTA_TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            sendAck(addrSrc, session_id_, p->seq_num, 3 /*no_space*/, chunks_received_);
            resetSession();
            return;
        }
    }

    chunk_bitmap_[p->seq_num / 8] |= bit;
    chunks_received_++;
    bytes_written_ += p->chunk_size;
    sendAck(addrSrc, session_id_, p->seq_num, 0, chunks_received_);

    ESP_LOGD(OTA_TAG, "Chunk %u/%u received", chunks_received_, total_chunks_);

    // Check if all chunks received
    if (chunks_received_ >= total_chunks_) {
        ESP_LOGI(OTA_TAG, "All chunks received, waiting for APPLY");
    }
}

// -- handleApply ------------------------------------------------------------
void OTAService::handleApply(const OTAApplyPayload* p, uint16_t addrSrc) {
    if (p->session_id != session_id_) return;
    if (chunks_received_ < total_chunks_) {
        ESP_LOGW(OTA_TAG, "APPLY requested but only %u/%u chunks received",
                 chunks_received_, total_chunks_);
        sendStatus(addrSrc);
        return;
    }

    if (is_delta_) {
        fclose(patch_file_);
        patch_file_ = nullptr;
        state_ = OTA_STATE_PATCHING;
        ESP_LOGI(OTA_TAG, "Starting delta patch application");
        xTaskCreatePinnedToCore(patchTask, "OTAPatch", 8192, NULL, 5,
                                &patch_task_handle_, 0);
    } else {
        esp_err_t err = esp_ota_end(ota_handle_);
        ota_handle_ = 0;
        if (err != ESP_OK) {
            ESP_LOGE(OTA_TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
            resetSession();
            return;
        }
        state_ = OTA_STATE_VERIFYING;
        if (verifyFirmwareCrc()) {
            state_ = OTA_STATE_REBOOTING;
            esp_err_t set_err = esp_ota_set_boot_partition(update_partition_);
            if (set_err != ESP_OK) {
                ESP_LOGE(OTA_TAG, "esp_ota_set_boot_partition failed");
                resetSession();
                return;
            }
            ESP_LOGI(OTA_TAG, "Full OTA complete, rebooting in 3s");
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            esp_restart();
        } else {
            ESP_LOGE(OTA_TAG, "CRC32 verification failed, aborting");
            resetSession();
        }
    }
}

// -- handleAbort ------------------------------------------------------------
void OTAService::handleAbort(const OTAAbortPayload* p, uint16_t addrSrc) {
    ESP_LOGW(OTA_TAG, "OTA ABORT received, reason=%u", p->reason);
    resetSession();
}

// -- verifyFirmwareCrc ------------------------------------------------------
bool OTAService::verifyFirmwareCrc() {
    if (!update_partition_) return false;
    uint32_t crc = 0;
    const size_t BLOCK = 512;
    uint8_t* buf = (uint8_t*)pvPortMalloc(BLOCK);
    if (!buf) return false;
    size_t remaining = total_size_;
    size_t offset = 0;
    while (remaining > 0) {
        size_t to_read = (remaining > BLOCK) ? BLOCK : remaining;
        esp_partition_read(update_partition_, offset, buf, to_read);
        crc = esp_crc32_le(crc, buf, to_read);
        offset += to_read;
        remaining -= to_read;
    }
    vPortFree(buf);
    bool ok = (crc == expected_crc32_);
    if (!ok) ESP_LOGE(OTA_TAG, "CRC32 mismatch: got 0x%08lX expected 0x%08lX",
                      (unsigned long)crc, (unsigned long)expected_crc32_);
    return ok;
}

// -- patchTask (stub -- Agent C fills in applyDeltaPatch) -------------------
void OTAService::patchTask(void* param) {
    OTAService& svc = OTAService::getInstance();
    svc.applyDeltaPatch();
    vTaskDelete(NULL);
}

// -- applyDeltaPatch --------------------------------------------------------
void OTAService::applyDeltaPatch() {
    ESP_LOGI(OTA_TAG, "applyDeltaPatch: starting");

    // Get the next OTA partition to write into
    update_partition_ = esp_ota_get_next_update_partition(NULL);
    if (!update_partition_) {
        ESP_LOGE(OTA_TAG, "applyDeltaPatch: no update partition");
        resetSession();
        return;
    }

    esp_err_t err = esp_ota_begin(update_partition_, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(OTA_TAG, "applyDeltaPatch: esp_ota_begin failed: %s", esp_err_to_name(err));
        resetSession();
        return;
    }

    // Open patch file from SPIFFS
    FILE* pf = fopen(OTA_SPIFFS_PATCH_PATH, "rb");
    if (!pf) {
        ESP_LOGE(OTA_TAG, "applyDeltaPatch: cannot open patch file");
        esp_ota_abort(ota_handle_);
        ota_handle_ = 0;
        resetSession();
        return;
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(OTA_TAG, "applyDeltaPatch: cannot get running partition");
        fclose(pf);
        esp_ota_abort(ota_handle_);
        ota_handle_ = 0;
        resetSession();
        return;
    }

#ifdef HAS_ESP_DELTA_OTA
    // Callbacks for esp_delta_ota — read from running partition, write to OTA handle
    struct DeltaCtx { const esp_partition_t* src; esp_ota_handle_t dst; };
    DeltaCtx ctx = { running, ota_handle_ };

    esp_delta_ota_cfg_t cfg = {};
    cfg.user_data = &ctx;
    cfg.read_cb_with_user_data = [](uint8_t* buf, size_t size, int offset, void* ud) -> esp_err_t {
        return esp_partition_read(static_cast<DeltaCtx*>(ud)->src, offset, buf, size);
    };
    cfg.write_cb_with_user_data = [](const uint8_t* buf, size_t size, void* ud) -> esp_err_t {
        return esp_ota_write(static_cast<DeltaCtx*>(ud)->dst, buf, size);
    };

    esp_delta_ota_handle_t delta_handle = esp_delta_ota_init(&cfg);
    esp_err_t apply_err = delta_handle ? ESP_OK : ESP_FAIL;
    if (apply_err != ESP_OK) {
        ESP_LOGE(OTA_TAG, "applyDeltaPatch: esp_delta_ota_init failed");
    }

    if (apply_err == ESP_OK) {
        const size_t PATCH_BUF_SIZE = 512;
        uint8_t* patch_buf = (uint8_t*)pvPortMalloc(PATCH_BUF_SIZE);
        if (!patch_buf) {
            apply_err = ESP_ERR_NO_MEM;
        } else {
            size_t n;
            while ((n = fread(patch_buf, 1, PATCH_BUF_SIZE, pf)) > 0) {
                apply_err = esp_delta_ota_feed_patch(delta_handle, patch_buf, (int)n);
                if (apply_err != ESP_OK) break;
            }
            vPortFree(patch_buf);
        }
    }

    if (apply_err == ESP_OK) {
        apply_err = esp_delta_ota_finalize(delta_handle);
    }
    if (delta_handle) {
        esp_delta_ota_deinit(delta_handle);
    }
#else
    esp_err_t apply_err = ESP_ERR_NOT_SUPPORTED;
    ESP_LOGE(OTA_TAG, "applyDeltaPatch: esp_delta_ota component not available");
#endif

    fclose(pf);
    remove(OTA_SPIFFS_PATCH_PATH);

    if (apply_err != ESP_OK) {
        ESP_LOGE(OTA_TAG, "applyDeltaPatch: failed: %s", esp_err_to_name(apply_err));
        esp_ota_abort(ota_handle_);
        ota_handle_ = 0;
        resetSession();
        return;
    }

    err = esp_ota_end(ota_handle_);
    ota_handle_ = 0;
    if (err != ESP_OK) {
        ESP_LOGE(OTA_TAG, "applyDeltaPatch: esp_ota_end failed: %s", esp_err_to_name(err));
        resetSession();
        return;
    }

    state_ = OTA_STATE_VERIFYING;
    if (verifyFirmwareCrc()) {
        state_ = OTA_STATE_REBOOTING;
        esp_err_t set_err = esp_ota_set_boot_partition(update_partition_);
        if (set_err != ESP_OK) {
            ESP_LOGE(OTA_TAG, "applyDeltaPatch: esp_ota_set_boot_partition failed");
            resetSession();
            return;
        }
        ESP_LOGI(OTA_TAG, "Delta OTA complete, rebooting in 3s");
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        esp_restart();
    } else {
        ESP_LOGE(OTA_TAG, "applyDeltaPatch: CRC32 verification failed");
        resetSession();
    }
}

// -- sendAck ----------------------------------------------------------------
void OTAService::sendAck(uint16_t addrDst, uint32_t sid, uint16_t seq,
                         uint8_t status, uint16_t next_expected) {
    uint32_t msgSize = sizeof(OTAAckPayload);
    DataMessage* msg = (DataMessage*)pvPortMalloc(sizeof(DataMessageGeneric) + msgSize);
    if (!msg) return;
    msg->appPortDst  = appPort::MQTTApp;
    msg->appPortSrc  = appPort::OTAApp;
    msg->addrSrc     = LoRaMeshService::getInstance().getLocalAddress();
    msg->addrDst     = addrDst;
    msg->messageSize = msgSize;
    OTAAckPayload* ack = (OTAAckPayload*)msg->message;
    ack->type          = OTA_ACK;
    ack->session_id    = sid;
    ack->seq_num       = seq;
    ack->status        = status;
    ack->next_expected = next_expected;
    MessageManager::getInstance().sendMessage(messagePort::MqttPort, msg);
    vPortFree(msg);
}

// -- sendStatus -------------------------------------------------------------
void OTAService::sendStatus(uint16_t addrDst) {
    uint32_t msgSize = sizeof(OTAStatusPayload);
    DataMessage* msg = (DataMessage*)pvPortMalloc(sizeof(DataMessageGeneric) + msgSize);
    if (!msg) return;
    msg->appPortDst  = appPort::MQTTApp;
    msg->appPortSrc  = appPort::OTAApp;
    msg->addrSrc     = LoRaMeshService::getInstance().getLocalAddress();
    msg->addrDst     = addrDst;
    msg->messageSize = msgSize;
    OTAStatusPayload* st = (OTAStatusPayload*)msg->message;
    st->type            = OTA_STATUS;
    st->session_id      = session_id_;
    st->state           = state_;
    st->chunks_received = chunks_received_;
    st->total_chunks    = total_chunks_;
    st->bytes_written   = bytes_written_;
    MessageManager::getInstance().sendMessage(messagePort::MqttPort, msg);
    vPortFree(msg);
}

// -- processReceivedMessage -------------------------------------------------
void OTAService::processReceivedMessage(messagePort port, DataMessage* message) {
    if (message->messageSize < 1) return;
    OTAMessageType msgType = (OTAMessageType)message->message[0];
    uint16_t addrSrc = message->addrSrc;

    ESP_LOGD(OTA_TAG, "Received OTA message type=%d from 0x%04X", msgType, addrSrc);

    switch (msgType) {
        case OTA_BEGIN:
            if (message->messageSize >= sizeof(OTABeginPayload))
                handleBegin((OTABeginPayload*)message->message, addrSrc);
            break;
        case OTA_CHUNK:
            if (message->messageSize >= sizeof(OTAChunkPayload))
                handleChunk((OTAChunkPayload*)message->message, addrSrc);
            break;
        case OTA_APPLY:
            if (message->messageSize >= sizeof(OTAApplyPayload))
                handleApply((OTAApplyPayload*)message->message, addrSrc);
            break;
        case OTA_ABORT:
            if (message->messageSize >= sizeof(OTAAbortPayload))
                handleAbort((OTAAbortPayload*)message->message, addrSrc);
            break;
        case OTA_STATUS:
            break;  // We don't process status messages sent to us
        default:
            ESP_LOGW(OTA_TAG, "Unknown OTA message type %d", msgType);
            break;
    }
}

// -- getJSON ----------------------------------------------------------------
// Called when sending OTA messages (ACK, STATUS, ABORT) from device to MQTT
String OTAService::getJSON(DataMessage* message) {
    StaticJsonDocument<512> doc;
    JsonObject data = doc.createNestedObject("data");
    message->serialize(data);

    if (message->messageSize > 0) {
        OTAMessageType msgType = (OTAMessageType)message->message[0];
        data["type"] = msgType;
        switch (msgType) {
            case OTA_ACK: {
                OTAAckPayload* p = (OTAAckPayload*)message->message;
                data["session_id"]    = p->session_id;
                data["seq_num"]       = p->seq_num;
                data["status"]        = p->status;
                data["next_expected"] = p->next_expected;
                break;
            }
            case OTA_STATUS: {
                OTAStatusPayload* p = (OTAStatusPayload*)message->message;
                data["session_id"]      = p->session_id;
                data["state"]           = p->state;
                data["chunks_received"] = p->chunks_received;
                data["total_chunks"]    = p->total_chunks;
                data["bytes_written"]   = p->bytes_written;
                break;
            }
            case OTA_ABORT: {
                OTAAbortPayload* p = (OTAAbortPayload*)message->message;
                data["session_id"] = p->session_id;
                data["reason"]     = p->reason;
                break;
            }
            default:
                break;
        }
    }

    String json;
    serializeJson(doc, json);
    return json;
}

// -- getDataMessage ---------------------------------------------------------
// Called when an MQTT message arrives and the manager routes it here by appPortSrc
DataMessage* OTAService::getDataMessage(JsonObject data) {
    OTAMessageType msgType = (OTAMessageType)(data["type"] | 0);
    DataMessage* msg = nullptr;

    switch (msgType) {
        case OTA_BEGIN: {
            uint32_t msgSize = sizeof(OTABeginPayload);
            msg = (DataMessage*)pvPortMalloc(sizeof(DataMessageGeneric) + msgSize);
            if (!msg) return nullptr;
            msg->messageSize = msgSize;
            OTABeginPayload* p = (OTABeginPayload*)msg->message;
            p->type         = OTA_BEGIN;
            p->session_id   = data["session_id"] | (uint32_t)0;
            p->total_size   = data["total_size"] | (uint32_t)0;
            p->total_chunks = data["total_chunks"] | 0;
            p->fw_crc32     = data["fw_crc32"] | (uint32_t)0;
            p->is_delta     = data["is_delta"] | 0;
            const char* ver = data["fw_version"] | "";
            strncpy(p->fw_version, ver, sizeof(p->fw_version) - 1);
            break;
        }
        case OTA_CHUNK: {
            const char* b64 = data["data_b64"] | "";
            size_t b64_len = strlen(b64);
            // Allocate temp buffer for base64 decode (worst case: b64_len * 3/4)
            size_t max_decoded = (b64_len / 4) * 3 + 4;
            uint8_t* decoded = (uint8_t*)pvPortMalloc(max_decoded);
            if (!decoded) return nullptr;
            size_t decoded_len = 0;
            int rc = mbedtls_base64_decode(decoded, max_decoded, &decoded_len,
                                           (const unsigned char*)b64, b64_len);
            if (rc != 0) {
                ESP_LOGE(OTA_TAG, "base64 decode failed: %d", rc);
                vPortFree(decoded);
                return nullptr;
            }
            uint32_t msgSize = (uint32_t)(sizeof(OTAChunkPayload) + decoded_len);
            msg = (DataMessage*)pvPortMalloc(sizeof(DataMessageGeneric) + msgSize);
            if (!msg) { vPortFree(decoded); return nullptr; }
            msg->messageSize = msgSize;
            OTAChunkPayload* p = (OTAChunkPayload*)msg->message;
            p->type       = OTA_CHUNK;
            p->session_id = data["session_id"] | (uint32_t)0;
            p->seq_num    = data["seq_num"] | 0;
            p->chunk_size = (uint16_t)decoded_len;
            p->crc16      = data["crc16"] | 0;
            memcpy(p->data, decoded, decoded_len);
            vPortFree(decoded);
            break;
        }
        case OTA_APPLY: {
            uint32_t msgSize = sizeof(OTAApplyPayload);
            msg = (DataMessage*)pvPortMalloc(sizeof(DataMessageGeneric) + msgSize);
            if (!msg) return nullptr;
            msg->messageSize = msgSize;
            OTAApplyPayload* p = (OTAApplyPayload*)msg->message;
            p->type       = OTA_APPLY;
            p->session_id = data["session_id"] | (uint32_t)0;
            break;
        }
        case OTA_ABORT: {
            uint32_t msgSize = sizeof(OTAAbortPayload);
            msg = (DataMessage*)pvPortMalloc(sizeof(DataMessageGeneric) + msgSize);
            if (!msg) return nullptr;
            msg->messageSize = msgSize;
            OTAAbortPayload* p = (OTAAbortPayload*)msg->message;
            p->type       = OTA_ABORT;
            p->session_id = data["session_id"] | (uint32_t)0;
            p->reason     = data["reason"] | 0;
            break;
        }
        default:
            ESP_LOGW(OTA_TAG, "getDataMessage: unknown type %d", msgType);
            return nullptr;
    }

    if (msg) {
        msg->appPortDst = appPort::OTAApp;
        msg->appPortSrc = appPort::OTAApp;
        msg->addrSrc    = data["addrSrc"] | 0;
        msg->addrDst    = data["addrDst"] | 0;
        msg->messageId  = data["messageId"] | 0;
    }
    return msg;
}

#endif // OTA_ENABLED
