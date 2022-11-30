#pragma once

#include <Arduino.h>

class Request {

public:
    Request(uint16_t src, uint8_t id): id(id) {};

private:
    uint16_t src;
    uint8_t id;
};