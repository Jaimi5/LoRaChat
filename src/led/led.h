#pragma once

#include <Arduino.h>


#include <ArduinoLog.h>

#include "sensor.h"

#include "message/messageService.h"

#include "ledCommandService.h"

#include "ledMessage.h"

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

    String ledOff();

    String getJSON(DataMessage* message);

    DataMessage* getDataMessage(JsonObject data);

    void processReceivedMessage(messagePort port, DataMessage* message);

private:
    Led(): MessageService(LedApp, "Led") {
        commandService = ledCommandService;
    };
};