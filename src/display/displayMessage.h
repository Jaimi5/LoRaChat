#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

static const char* DISPLAY_TAG = "DisplayService";

#pragma pack(1)

enum DisplayCommand: uint8_t {
    DisplayOn = 0,
    DisplayOff = 1,
    DisplayBlink = 2,
    DisplayClear = 3,
    DisplayText = 4,
    DisplayLogo = 5
};

class DisplayMessage: public DataMessageGeneric {
public:
    DisplayCommand displayCommand;
    union {
        char displayText[128];
    };

    uint32_t getDisplayTextSize() {
        return this->messageSize - sizeof(DisplayCommand);
    }

    void serialize(JsonObject& doc) {
        ESP_LOGE(DISPLAY_TAG, "Display Message not implemented");
    }

    void deserialize(JsonObject& doc) {
        // Call the base class deserialize function
        ((DataMessageGeneric*) (this))->deserialize(doc);

        // Add the derived class data to the JSON object
        displayCommand = doc["displayCommand"];

        size_t displayTextSize = 0;

        switch (displayCommand) {
            case DisplayText: {
                    displayTextSize = strlen(doc["displayText"]);
                    if (displayTextSize > 32) {
                        ESP_LOGE(DISPLAY_TAG, "displayText is too long");
                        return;
                    }
                    strcpy(displayText, doc["displayText"]);
                    break;
                }
            default:
                break;
        }

        this->messageSize = displayTextSize + sizeof(DisplayCommand);

    }
};
#pragma pack()