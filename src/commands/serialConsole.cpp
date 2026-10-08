#include "serialConsole.h"

#include <Arduino.h>

#include "lineReader.h"
#include "message/messageManager.h"

namespace {

LineReader lineReader;

void onSerialReceive() {
    std::string line;
    while (Serial.available() > 0) {
        if (!lineReader.push(static_cast<char>(Serial.read()), line)) continue;
        String reply = MessageManager::getInstance().executeCommand(String(line.c_str()));
        Serial.printf("> %s\n%s\n", line.c_str(), reply.c_str());
    }
}

}  // namespace

void SerialConsole::begin() {
    Serial.onReceive(onSerialReceive);
}
