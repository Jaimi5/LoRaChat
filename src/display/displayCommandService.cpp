#include "displayCommandService.h"
#include "displayService.h"

namespace {

/** @return the address given as the first word in hex (without 0x), or 0 for this node. */
uint16_t commandAddress(const String& args) {
    String first = args.substring(0, args.indexOf(' '));
    if (first.length() == 0) return 0;
    for (size_t i = 0; i < first.length(); i++) {
        if (!isHexadecimalDigit(first.charAt(i))) return 0;
    }
    return strtol(first.c_str(), NULL, 16);
}

}  // namespace

DisplayCommandService::DisplayCommandService() {
    addCommand(Command(
        "/displayOn", "Set the Display On specifying the destination in hex (like the display)",
        DisplayCommand::DisplayOn, Perm::OPEN, [this](String args) {
            return String(DisplayService::getInstance().displayOn(commandAddress(args)));
        }));
    addCommand(Command(
        "/displayOff", "Set the Display Off specifying the destination in hex (like the display)",
        DisplayCommand::DisplayOff, Perm::OPEN, [this](String args) {
            return String(DisplayService::getInstance().displayOff(commandAddress(args)));
        }));
    addCommand(Command("/displayBlink",
                       "Set the Display Blink specifying the destination in hex (like the display)",
                       DisplayCommand::DisplayBlink, Perm::OPEN, [this](String args) {
                           return String(
                               DisplayService::getInstance().displayBlink(commandAddress(args)));
                       }));
    addCommand(Command(
        "/displayClear", "Clear the Display specifying the destination in hex (like the display)",
        DisplayCommand::DisplayClear, Perm::OPEN, [this](String args) {
            return String(DisplayService::getInstance().clearDisplay(commandAddress(args)));
        }));
    addCommand(Command(
        "/displayLogo", "Display Logo specifying the destination in hex (like the display)",
        DisplayCommand::DisplayLogo, Perm::OPEN, [this](String args) {
            return String(DisplayService::getInstance().displayLogo(commandAddress(args)));
        }));
    addCommand(Command("/displayText",
                       "Display Text specifying the destination in hex (like the display)",
                       DisplayCommand::DisplayText, Perm::OPEN, [this](String args) {
                           uint16_t address = commandAddress(args);
                           if (address > 0) {
                               args = args.substring(5);
                           }

                           return String(DisplayService::getInstance().displayText(address, args));
                       }));
}