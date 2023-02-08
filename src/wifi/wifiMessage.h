#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#pragma pack(1)

enum WiFiMessageType: uint8_t {
    addSSID = 1,
    addPSD = 2,
    saveConfig = 3,
    connectWiFi = 4,
    startServer = 5,
    stopServer = 6,
    getIP = 7,
    resetConfig = 8
};

class WiFiMessage: public DataMessageGeneric {
public:
    uint8_t url;
    uint8_t payload[];
};
#pragma pack()