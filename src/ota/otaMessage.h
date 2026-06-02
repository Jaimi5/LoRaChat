#pragma once
#include <Arduino.h>
#include "message/dataMessage.h"

#pragma pack(1)

enum OTAMessageType : uint8_t {
    OTA_BEGIN  = 1,
    OTA_CHUNK  = 2,
    OTA_ACK    = 3,
    OTA_APPLY  = 4,
    OTA_STATUS = 5,
    OTA_ABORT  = 6,
};

// Host->Node: initiate OTA session
struct OTABeginPayload {
    OTAMessageType type;   // OTA_BEGIN
    uint32_t session_id;
    uint32_t total_size;
    uint16_t total_chunks;
    uint32_t fw_crc32;
    uint8_t  is_delta;     // 1 = delta patch, 0 = full firmware
    char     fw_version[16];
};

// Host->Node: firmware/patch data chunk
struct OTAChunkPayload {
    OTAMessageType type;   // OTA_CHUNK
    uint32_t session_id;
    uint16_t seq_num;
    uint16_t chunk_size;
    uint16_t crc16;
    uint8_t  data[];
};

// Node->Host: acknowledge chunk or event
struct OTAAckPayload {
    OTAMessageType type;   // OTA_ACK
    uint32_t session_id;
    uint16_t seq_num;
    uint8_t  status;       // 0=ok, 1=crc_error, 2=out_of_order, 3=no_space
    uint16_t next_expected;
};

// Host->Node: trigger apply after all chunks received
struct OTAApplyPayload {
    OTAMessageType type;   // OTA_APPLY
    uint32_t session_id;
};

// Node->Host: current OTA state report
struct OTAStatusPayload {
    OTAMessageType type;   // OTA_STATUS
    uint32_t session_id;
    uint8_t  state;
    uint16_t chunks_received;
    uint16_t total_chunks;
    uint32_t bytes_written;
};

// Bidirectional: abort session
struct OTAAbortPayload {
    OTAMessageType type;   // OTA_ABORT
    uint32_t session_id;
    uint8_t  reason;
};

#pragma pack()
