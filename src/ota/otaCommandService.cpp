#include "otaCommandService.h"

#include "otaService.h"

namespace {
constexpr uint8_t COMMAND_KEY = 1;
constexpr uint8_t COMMAND_KEY_SET = 2;
constexpr uint8_t COMMAND_MAINT_OPEN = 3;
constexpr uint8_t COMMAND_MAINT_CLOSE = 4;
}  // namespace

OtaCommandService::OtaCommandService() {
    addCommand(Command("/key", "Show whether a deployment key is stored, and its fingerprint",
                       COMMAND_KEY, 1,
                       [](String) { return OtaService::getInstance().describeKey(); }));
    addCommand(Command("/key.set", "Store the deployment key: /key.set <64 hex digits>",
                       COMMAND_KEY_SET, 0,
                       [](String args) { return OtaService::getInstance().setKey(args); }));
    addCommand(Command("/maint.open",
                       "Open a maintenance window: /maint.open <seconds> [ap]. Without ap the "
                       "node joins its WiFi network and checks for updates; with ap it opens "
                       "its own access point for an upload",
                       COMMAND_MAINT_OPEN, 1,
                       [](String args) { return OtaService::getInstance().openWindow(args); }));
    addCommand(Command("/maint.close", "Close the maintenance window", COMMAND_MAINT_CLOSE, 1,
                       [](String) { return OtaService::getInstance().closeWindow(); }));
}
