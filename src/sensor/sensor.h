#pragma once

#include <Arduino.h>

#include "config.h"

#include "message/messageManager.h"

#include "message/messageService.h"

template <typename T>
class Sensor: public MessageService {
protected:
    uint32_t readEveryMs = 0;

    uint8_t sensorMessageId = 0;

public:
    Sensor(uint8_t id, String name, uint32_t readEveryMs): MessageService(id, name) {
        this->readEveryMs = readEveryMs;
    };

    virtual void init() = 0;
    virtual void start() = 0;
    virtual void pause() = 0;

    virtual T readValue() = 0;
    virtual T readValueWait(uint8_t retries) = 0;

protected:
    void setReadEveryMs(uint32_t readEveryMs) {
        this->readEveryMs = readEveryMs;
    }

    bool running = false;
};