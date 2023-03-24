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

    virtual void processReceivedMessage(messagePort port, DataMessage* message) {
        Log.error("processReceivedMessage not implemented for service %s" CR, serviceName);
    };

    virtual String getJSON(DataMessage* message) {
        Log.error("getJSON not implemented for service %s" CR, serviceName);
        return "";
    };

    TaskHandle_t receiveMessage_TaskHandle = NULL;

    xQueueHandle xQueueReceived;

    uint8_t serviceId;

    String serviceName;

    CommandService* commandService;

    String toString() { return "Id: " + String(serviceId) + " - " + serviceName; }

    virtual DataMessage* getDataMessage(JsonObject data) {
        Log.error("getDataMessage not implemented for service %s" CR, serviceName);
        return nullptr;
    };

    virtual DataMessage* getDataMessage(JsonObject data, DataMessage* message) {
        Log.error("getDataMessage not implemented for service %s" CR, serviceName);
        return nullptr;
    };
};