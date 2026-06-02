#pragma once
#include <Arduino.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#if __has_include("esp_delta_ota.h")
#include "esp_delta_ota.h"
#define HAS_ESP_DELTA_OTA 1
#endif
// esp_delta_ota loaded via src/idf_component.yml (espressif/esp_delta_ota)
#include "message/messageService.h"
#include "message/messageManager.h"
#include "otaMessage.h"
#include "otaCommandService.h"
#include "config.h"

#ifdef OTA_ENABLED

enum OTAState : uint8_t {
    OTA_STATE_IDLE             = 0,
    OTA_STATE_RECEIVING_CHUNKS = 1,
    OTA_STATE_PATCHING         = 2,
    OTA_STATE_VERIFYING        = 3,
    OTA_STATE_REBOOTING        = 4,
};

class OTAService : public MessageService {
public:
    static OTAService& getInstance() {
        static OTAService instance;
        return instance;
    }

    void init();
    String getJSON(DataMessage* message) override;
    DataMessage* getDataMessage(JsonObject data) override;
    void processReceivedMessage(messagePort port, DataMessage* message) override;

    OTACommandService* otaCommandService_ = new OTACommandService();

private:
    OTAService() : MessageService(OTAApp, "OTA") { commandService = otaCommandService_; }

    void handleBegin(const OTABeginPayload* payload, uint16_t addrSrc);
    void handleChunk(const OTAChunkPayload* payload, uint16_t addrSrc);
    void handleApply(const OTAApplyPayload* payload, uint16_t addrSrc);
    void handleAbort(const OTAAbortPayload* payload, uint16_t addrSrc);
    void sendAck(uint16_t addrDst, uint32_t session_id, uint16_t seq_num, uint8_t status, uint16_t next_expected);
    void sendStatus(uint16_t addrDst);
    void resetSession();
    bool verifyFirmwareCrc();
    void applyDeltaPatch();   // implemented by Agent C
    static void watchdogTask(void* param);
    static void patchTask(void* param);

    OTAState    state_           = OTA_STATE_IDLE;
    uint32_t    session_id_      = 0;
    uint16_t    total_chunks_    = 0;
    uint32_t    total_size_      = 0;
    uint32_t    expected_crc32_  = 0;
    uint8_t     is_delta_        = 0;
    char        fw_version_[16]  = {};
    uint16_t    chunks_received_ = 0;
    uint32_t    bytes_written_   = 0;
    uint32_t    last_activity_ms_= 0;
    uint16_t    addr_host_       = 0;
    uint8_t*    chunk_bitmap_    = nullptr;

    esp_ota_handle_t            ota_handle_       = 0;
    const esp_partition_t*      update_partition_ = nullptr;
    FILE*                       patch_file_       = nullptr;

    TaskHandle_t watchdog_task_handle_ = nullptr;
    TaskHandle_t patch_task_handle_    = nullptr;
};

#endif // OTA_ENABLED
