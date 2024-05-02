#include "displayCommandService.h"
#include "displayService.h"
#include "displayCommandService.h"

DisplayCommandService::DisplayCommandService() {
    addCommand(Command("/displayOn", "Set the Display On", DisplayCommand::DisplayOn, 1,
        [this](String args) {

        return String(DisplayService::getInstance().displayOn());
    }));
    addCommand(Command("/displayOff", "Set the Display Off", DisplayCommand::DisplayOff, 1,
        [this](String args) {
        return String(DisplayService::getInstance().displayOff());
    }));
    addCommand(Command("/displayBlink", "Set the Display Blink", DisplayCommand::DisplayBlink, 1,
        [this](String args) {
        return String(DisplayService::getInstance().displayBlink());
    }));
    addCommand(Command("/displayClear", "Clear the Display", DisplayCommand::DisplayClear, 1,
        [this](String args) {
        return String(DisplayService::getInstance().clearDisplay());
    }));
    addCommand(Command("/displayLogo", "Display Logo", DisplayCommand::DisplayLogo, 1,
        [this](String args) {
        return String(DisplayService::getInstance().displayLogo());
    }));
    addCommand(Command("/displayText", "Display Text", DisplayCommand::DisplayText, 1,
        [this](String args) {
        return String(DisplayService::getInstance().displayText(args));
    }));
}