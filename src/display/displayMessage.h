#pragma once

#include <Arduino.h>

#include <algorithm>

#include "message/dataMessage.h"

#pragma pack(1)

enum DisplayCommand : uint8_t {
    DisplayOn = 0,
    DisplayOff = 1,
    DisplayBlink = 2,
    DisplayClear = 3,
    DisplayText = 4,
    DisplayLogo = 5
};

class DisplayMessage : public DataMessageGeneric {
public:
    DisplayCommand displayCommand;
    union {
        char displayText[128];
    };

    /** @return the bytes of displayText that the message carries, NUL included if sent. */
    uint32_t getDisplayTextSize() {
        if (this->messageSize <= sizeof(DisplayCommand)) return 0;
        return std::min<uint32_t>(this->messageSize - sizeof(DisplayCommand), sizeof(displayText));
    }
};
#pragma pack()