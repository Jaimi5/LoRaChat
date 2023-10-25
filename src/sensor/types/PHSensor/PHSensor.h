#pragma once

#include <Arduino.h>

#include <Wire.h> //I2C Arduino Library

// SINCE EZO_i2c library declares a macro called NO_DATA, we need to undefine it before including the library. Yeah... I know... It's ugly :(
// #pragma push_macro("NO_DATA")
// #undef NO_DATA
// #include <Ezo_i2c.h> // Link: https://github.com/Atlas-Scientific/Ezo_I2c_lib)
// #pragma pop_macro("NO_DATA")

#include "PHSensorMessage.h"

class PHSensor {
public:
    void init();

    PHSensorMessage read();

private:
    // Ezo_board RTD = Ezo_board(102, "RTD");  //Temperature, create a PH circuit object, who's address is 99 and name is "PH"
    // Ezo_board PH = Ezo_board(99, "PH");  //PH, create a PH circuit object, who's address is 99 and name is "PH"


    // float receive_reading(Ezo_board& board);

    bool initialized = false;
};

