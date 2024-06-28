#pragma once

#include <Arduino.h>

#include "message/messageService.h"

#include "message/messageManager.h"

#include "ledCommandService.h"

#include "ledMessage.h"

#include "config.h"

#include "LoraMesher.h"

class Led: public MessageService {
public:
    /**
     * @brief Construct a new GPSService object
     *
     */
    static Led& getInstance() {
        static Led instance;
        return instance;
    }

    LedCommandService* ledCommandService = new LedCommandService();

    void init();

    String ledOn();

    String ledOn(uint16_t dst);

    String ledOff();

    String ledOff(uint16_t dst);

    String ledBlink();

    String getJSON(DataMessage* message);

    DataMessage* getDataMessage(JsonObject data);

    DataMessage* getLedMessage(LedCommand command, uint16_t dst);

    void processReceivedMessage(messagePort port, DataMessage* message);

private:
    Led(): MessageService(LedApp, "Led") {
        commandService = ledCommandService;
    };

    uint8_t state = 0;
};