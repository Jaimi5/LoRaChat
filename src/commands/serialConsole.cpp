#include "serialConsole.h"

#include <Arduino.h>

#include "commandRouter.h"
#include "lineReader.h"

namespace {

LineReader lineReader;

void onSerialReceive() {
    std::string line;
    while (Serial.available() > 0) {
        if (!lineReader.push(static_cast<char>(Serial.read()), line)) continue;
        CommandRequest request;
        request.origin = Origin::SERIAL_CONSOLE;
        if (!parseCommandLine(line, request.command)) {
            Serial.println("> ?\nMalformed line: @<dst> <command> [#<counter>.<tag>]");
            continue;
        }
        if (!CommandRouter::getInstance().submit(request)) Serial.println("> ?\nBusy, line dropped");
    }
}

}  // namespace

void SerialConsole::begin() {
    Serial.onReceive(onSerialReceive);
}
