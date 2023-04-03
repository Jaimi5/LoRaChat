#include "ledCommandService.h"
#include "led.h"

LedCommandService::LedCommandService() {
    addCommand(Command("/ledOn", "Set the Led On", LedCommand::On, 1,
        [this](String args) {
        return String(Led::getInstance().ledOn());
    }));

    addCommand(Command("/ledOff", "Set the Led Off", LedCommand::Off, 1,
        [this](String args) {
        return String(Led::getInstance().ledOff());
    }));
}