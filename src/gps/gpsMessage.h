#pragma once

#include <Arduino.h>

#include "./message/dataMessage.h"

enum GPSMessageType: uint8_t {
    getGPS = 1
};