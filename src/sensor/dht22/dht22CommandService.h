#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "sensor/sensor.h"

#include "sensor/sensorMessage.h"

class Dht22CommandService: public CommandService {
public:

    Dht22CommandService();
};