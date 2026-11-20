#include "displayCommandService.h"
#include "displayService.h"

#include "cmdText.h"
#include "commands/commandRouter.h"
#include "loramesh/loraMeshService.h"

namespace {

/**
 * Adds the handlers and the errors of display command @p command to its declaration.
 *
 * On the serial console the arguments may start with a destination, "<addr> [<text>]": the
 * first word is one only when it has exactly four hex digits. The command then goes to that
 * node as a display message. Every other line runs on this node.
 */
Command displayCommand(Command declared, DisplayCommand command) {
    return declared
        .run([command](const Fields& in, Fields&) {
            return DisplayService::getInstance().run(command, in.text(0).c_str());
        })
        .runText([command](String args, const Command& self) {
            uint16_t dst = 0;
            std::string rest;
            const bool console =
                CommandRouter::getInstance().runningOrigin() == Origin::SERIAL_CONSOLE;
            // An address alone is the text of /displayText: there is nothing to send to that node.
            if (!console || !parseAddressedArgs(args.c_str(), dst, rest) ||
                (command == DisplayText && rest.empty())) {
                return self.runTyped(args);
            }
            if (dst == LoRaMeshService::getInstance().getLocalAddress()) {
                return self.runTyped(rest.c_str());
            }
            return DisplayService::getInstance().sendRemote(command, dst, rest.c_str());
        })
        .withCode(DISPLAY_ERR_NO_DISPLAY, "The node has no working display")
        .withCode(DISPLAY_ERR_TEXT_TOO_LONG, "The text is too long to send to another node")
        .withCode(DISPLAY_ERR_NOT_SENT, "The command could not be sent on");
}

}  // namespace

DisplayCommandService::DisplayCommandService() {
    addCommand(
        displayCommand(Command(0x60, "/displayOn", "Turns the display on for a minute", Perm::OPEN)
                           .reply("", "Display On"),
                       DisplayOn));
    addCommand(displayCommand(
        Command(0x61, "/displayOff", "Turns the display off", Perm::OPEN).reply("", "Display Off"),
        DisplayOff));
    addCommand(displayCommand(
        Command(0x62, "/displayBlink", "Blinks the display", Perm::OPEN).reply("", "Display Blink"),
        DisplayBlink));
    addCommand(displayCommand(
        Command(0x63, "/displayClear", "Removes the text line from the display", Perm::OPEN)
            .reply("", "Display Clear"),
        DisplayClear));
    addCommand(
        displayCommand(Command(0x64, "/displayLogo", "Shows the logo for a second", Perm::OPEN)
                           .reply("", "Display Logo"),
                       DisplayLogo));
    addCommand(displayCommand(
        Command(0x65, "/displayText", "Shows a line of text for five minutes", Perm::OPEN)
            .args("text:s")
            .reply("", "Display Text"),
        DisplayText));
}
