#pragma once

#include <Arduino.h>

#include <algorithm>

#include "message/dataMessage.h"

static const char* DISPLAY_TAG = "DisplayService";

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

    void serialize(JsonObject& doc) { ESP_LOGE(DISPLAY_TAG, "Display Message not implemented"); }

    void deserialize(JsonObject& doc) {
        // Call the base class deserialize function
        ((DataMessageGeneric*)(this))->deserialize(doc);

        // Add the derived class data to the JSON object
        displayCommand = doc["displayCommand"];

        size_t displayTextSize = 0;

        switch (displayCommand) {
            case DisplayText: {
                const char* text = doc["displayText"] | "";
                size_t length = strlen(text);
                if (length > 32) {
                    ESP_LOGE(DISPLAY_TAG, "displayText is too long");
                    break;
                }
                memcpy(displayText, text, length + 1);
                displayTextSize = length + 1;
                break;
            }
            default:
                break;
        }

        this->messageSize = displayTextSize + sizeof(DisplayCommand);
    }
};
#pragma pack()