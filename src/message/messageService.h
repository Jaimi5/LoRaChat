#pragma once

#include <Arduino.h>

#include <ArduinoLog.h>

#include "dataMessage.h"

#include "commands/commandService.h"

class MessageService {
public:

    MessageService(uint8_t id, String name) {
        serviceId = id;
        serviceName = name;
    };

    virtual void processReceivedMessage(messagePort port, DataMessage* message) {};

    virtual String getJSON(DataMessage* message) { return ""; };

    TaskHandle_t receiveMessage_TaskHandle = NULL;

    xQueueHandle xQueueReceived;

    uint8_t serviceId;

    String serviceName;

    CommandService* commandService;

    String toString() { return "Id: " + String(serviceId) + " - " + serviceName; }

};