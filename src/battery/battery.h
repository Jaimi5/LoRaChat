#ifndef BATTERY_APP
#define BATTERY_APP

#include <Arduino.h>

#include "config.h"

class Battery {
public:

    static Battery& getInstance() {
        static Battery instance;
        return instance;
    }

    void init();

    float getVoltagePercentage();

private:
    Battery() {};
};

#endif