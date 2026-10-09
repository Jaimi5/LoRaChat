#include "serialConsole.h"

#include <Arduino.h>

#include <cstring>

#include "lineReader.h"
#include "message/messageManager.h"

namespace {

const char* SC_TAG = "SerialConsole";
constexpr uint32_t CONSOLE_TASK_STACK = 6144;
constexpr UBaseType_t QUEUED_LINES = 4;

struct Line {
    char text[LineReader::MAX_LINE + 1];
};

LineReader lineReader;
QueueHandle_t lines = nullptr;

void onSerialReceive() {
    std::string line;
    while (Serial.available() > 0) {
        if (!lineReader.push(static_cast<char>(Serial.read()), line)) continue;
        Line item = {};
        std::strncpy(item.text, line.c_str(), sizeof(item.text) - 1);
        if (xQueueSend(lines, &item, 0) != pdTRUE) ESP_LOGW(SC_TAG, "Console busy, line dropped");
    }
}

void consoleTask(void*) {
    Line item;
    for (;;) {
        if (xQueueReceive(lines, &item, portMAX_DELAY) != pdTRUE) continue;
        String reply = MessageManager::getInstance().executeCommand(String(item.text));
        const char* space = std::strchr(item.text, ' ');
        int nameLength = space ? static_cast<int>(space - item.text) : std::strlen(item.text);
        Serial.printf("> %.*s\n%s\n", nameLength, item.text, reply.c_str());
    }
}

}  // namespace

void SerialConsole::begin() {
    lines = xQueueCreate(QUEUED_LINES, sizeof(Line));
    if (lines == nullptr ||
        xTaskCreate(consoleTask, "SerialCmd", CONSOLE_TASK_STACK, nullptr, 1, nullptr) != pdPASS) {
        ESP_LOGE(SC_TAG, "Serial console could not be started");
        return;
    }
    Serial.onReceive(onSerialReceive);
}
