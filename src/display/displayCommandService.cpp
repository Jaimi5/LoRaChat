#include "displayCommandService.h"
#include "displayService.h"

#include "cmdText.h"

namespace {

/**
 * Runs @p command with the arguments "[<addr>] [<text>]". The first word is the destination
 * only when it has exactly four hex digits. Anything else runs on this node.
 */
CommandReply run(DisplayCommand command, const String& args) {
    uint16_t dst = 0;
    std::string text = args.c_str();
    std::string rest;
    // An address alone is the text of /displayText: there is nothing to send to that node.
    if (parseAddressedArgs(text, dst, rest) && (command != DisplayText || !rest.empty())) {
        text = rest;
    } else {
        dst = 0;
    }
    return DisplayService::getInstance().execute(command, dst, text.c_str());
}

void add(CommandService& service, const char* name, const char* usage, const char* description,
         DisplayCommand command) {
    service.addCommand(
        Command(name, description, Perm::OPEN,
                [command](String args) { return run(command, args); })
            .withUsage(usage)
            .withCode(DISPLAY_ERR_NO_DISPLAY, "The node has no working display")
            .withCode(DISPLAY_ERR_TEXT_TOO_LONG, "The text is too long to send to another node")
            .withCode(DISPLAY_ERR_NOT_SENT, "The command could not be sent on"));
}

}  // namespace

DisplayCommandService::DisplayCommandService() {
    add(*this, "/displayOn", "[<addr>]", "Turns the display on for a minute", DisplayOn);
    add(*this, "/displayOff", "[<addr>]", "Turns the display off", DisplayOff);
    add(*this, "/displayBlink", "[<addr>]", "Blinks the display", DisplayBlink);
    add(*this, "/displayClear", "[<addr>]", "Removes the text line from the display", DisplayClear);
    add(*this, "/displayLogo", "[<addr>]", "Shows the logo for a second", DisplayLogo);
    add(*this, "/displayText", "[<addr>] <text>",
        "Shows a line of text for five minutes. <addr> has four hex digits", DisplayText);
}
