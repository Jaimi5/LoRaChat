#include "ledCommandService.h"
#include "led.h"

LedCommandService::LedCommandService() {
    addCommand(Command("/ledOn", "Set the Led On specifying the destination in hex (like the display)", LedCommand::On, 1,
        [this](String args) {
        return String(Led::getInstance().ledOn(strtol(args.c_str(), NULL, 16)));
    }));

    addCommand(Command("/ledOff", "Set the Led Off specifying the destination in hex (like the display)", LedCommand::Off, 1,
        [this](String args) {
        return String(Led::getInstance().ledOff(strtol(args.c_str(), NULL, 16)));
    }));
}