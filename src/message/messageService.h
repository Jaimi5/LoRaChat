#pragma once

#include <Arduino.h>

#include "dataMessage.h"

#include "commands/commandService.h"

static const char* MS_TAG = "MessageService";

class MessageService {
public:

    MessageService(uint8_t id, String name) {
        serviceId = id;
        serviceName = name;
    };

    virtual void processReceivedMessage(messagePort port, DataMessage* message) {
        ESP_LOGE(MS_TAG, "processReceivedMessage not implemented for service %s", serviceName.c_str());
    };

    virtual String getJSON(DataMessage* message) {
        ESP_LOGE(MS_TAG, "getJSON not implemented for service %s", serviceName.c_str());
        return "";
    };

    TaskHandle_t receiveMessage_TaskHandle = NULL;

    xQueueHandle xQueueReceived;

    uint8_t serviceId;

    String serviceName;

    CommandService* commandService;

    String toString() { return "Id: " + String(serviceId) + " - " + serviceName; }

    virtual DataMessage* getDataMessage(JsonObject data) {
        ESP_LOGE(MS_TAG, "getDataMessage not implemented for service %s", serviceName.c_str());
        return nullptr;
    };

    virtual DataMessage* getDataMessage(JsonObject data, DataMessage* message) {
        ESP_LOGE(MS_TAG, "getDataMessage not implemented for service %s", serviceName.c_str());
        return nullptr;
    };
};