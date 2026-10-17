#include "serialConsole.h"

#include <Arduino.h>

#include "commandRouter.h"
#include "lineReader.h"

static const char* SC_TAG = "SerialConsole";
static constexpr uint32_t TASK_STACK = 4096;

namespace {

LineReader lineReader;
TaskHandle_t consoleTask = nullptr;

void readLines() {
    std::string line;
    while (Serial.available() > 0) {
        if (!lineReader.push(static_cast<char>(Serial.read()), line)) continue;
        CommandRequest request;
        request.origin = Origin::SERIAL_CONSOLE;
        if (!parseCommandLine(line, request.command)) {
            Serial.println("> ?\nMalformed line: @<dst> <command> [#<counter>.<tag>]");
            continue;
        }
        if (!CommandRouter::getInstance().submit(request)) {
            Serial.println("> ?\nBusy, line dropped");
        }
    }
}

void consoleLoop(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        readLines();
    }
}

void onSerialReceive() {
    xTaskNotifyGive(consoleTask);
}

}  // namespace

void SerialConsole::begin() {
    if (xTaskCreate(consoleLoop, "SerialConsole", TASK_STACK, nullptr, 1, &consoleTask) != pdPASS) {
        ESP_LOGE(SC_TAG, "Serial console task could not be started");
        return;
    }
    Serial.onReceive(onSerialReceive);
}
