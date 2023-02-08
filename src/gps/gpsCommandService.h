#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "gpsMessage.h"

class GPSCommandService: public CommandService {
public:

    GPSCommandService();
};