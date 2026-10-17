#pragma once

#include <Arduino.h>

#include "dataMessage.h"

#include "commands/commandService.h"

static const char* MS_TAG = "MessageService";

class MessageService {
public:
    MessageService(uint8_t id, const String& name) : serviceId(id), serviceName(name) {}
    virtual ~MessageService() = default;
    MessageService(const MessageService&) = delete;
    MessageService& operator=(const MessageService&) = delete;

    /** Handles @p message, which stays owned by the caller. */
    virtual void processReceivedMessage(messagePort port, DataMessage* message) {
        ESP_LOGE(MS_TAG, "processReceivedMessage not implemented for service %s",
                 serviceName.c_str());
    };

    virtual String getJSON(DataMessage* message) {
        ESP_LOGE(MS_TAG, "getJSON not implemented for service %s", serviceName.c_str());
        return "";
    };

    uint8_t serviceId;

    String serviceName;

    CommandService* commandService = nullptr;

    String toString() { return "Id: " + String(serviceId) + " - " + serviceName; }

    /** @return the message that @p data describes, or nullptr if this service accepts none. */
    virtual DataMessagePtr getDataMessage(JsonObject data) {
        ESP_LOGE(MS_TAG, "getDataMessage not implemented for service %s", serviceName.c_str());
        return nullptr;
    };
};
