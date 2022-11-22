#pragma once

#include <Arduino.h>
#include "./message/dataMessage.h"

class App {
public:
    App();

    void setup();

    void loop();

    void getMessage(DataMessage *message);

};

