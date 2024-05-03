#include "displayCommandService.h"
#include "displayService.h"
#include "displayCommandService.h"

DisplayCommandService::DisplayCommandService() {
    addCommand(Command("/displayOn", "Set the Display On specifying the destination in hex (like the display)", DisplayCommand::DisplayOn, 1,
        [this](String args) {

        return String(DisplayService::getInstance().displayOn(getCommandAddress(args)));
    }));
    addCommand(Command("/displayOff", "Set the Display Off specifying the destination in hex (like the display)", DisplayCommand::DisplayOff, 1,
        [this](String args) {
        return String(DisplayService::getInstance().displayOff(getCommandAddress(args)));
    }));
    addCommand(Command("/displayBlink", "Set the Display Blink specifying the destination in hex (like the display)", DisplayCommand::DisplayBlink, 1,
        [this](String args) {
        return String(DisplayService::getInstance().displayBlink(getCommandAddress(args)));
    }));
    addCommand(Command("/displayClear", "Clear the Display specifying the destination in hex (like the display)", DisplayCommand::DisplayClear, 1,
        [this](String args) {
        return String(DisplayService::getInstance().clearDisplay(getCommandAddress(args)));
    }));
    addCommand(Command("/displayLogo", "Display Logo specifying the destination in hex (like the display)", DisplayCommand::DisplayLogo, 1,
        [this](String args) {
        return String(DisplayService::getInstance().displayLogo(getCommandAddress(args)));
    }));
    addCommand(Command("/displayText", "Display Text specifying the destination in hex (like the display)", DisplayCommand::DisplayText, 1,
        [this](String args) {
        uint16_t address = getCommandAddress(args);
        if (address > 0) {
            args = args.substring(5);
        }

        return String(DisplayService::getInstance().displayText(address, args));
    }));
}