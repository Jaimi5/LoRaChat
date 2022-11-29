#pragma once

#include <Arduino.h>

#include <ArduinoLog.h>

#include "dataMessage.h"

#include "./commands/commandService.h"

class MessageService {
public:

    MessageService(uint8_t id, String name);

    virtual void processReceivedMessage(messagePort port, DataMessage* message) {};

    TaskHandle_t receiveMessage_TaskHandle = NULL;

    xQueueHandle xQueueReceived;

    uint8_t serviceId;

    String serviceName;

    CommandService* commandService;

};