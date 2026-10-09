#include "otaCommandService.h"

#include "otaService.h"

namespace {
constexpr uint8_t COMMAND_KEY = 1;
constexpr uint8_t COMMAND_KEY_SET = 2;
constexpr uint8_t COMMAND_MAINT_OPEN = 3;
constexpr uint8_t COMMAND_MAINT_CLOSE = 4;
constexpr uint8_t COMMAND_VERSION = 5;
constexpr uint8_t COMMAND_OTA_STATUS = 6;
constexpr uint8_t COMMAND_REBOOT = 7;
constexpr uint8_t COMMAND_OTA_SERVER = 8;
constexpr uint8_t COMMAND_MAINT_WIFI = 9;
}  // namespace

OtaCommandService::OtaCommandService() {
    addCommand(Command("/key", "Show whether a deployment key is stored, and its fingerprint",
                       COMMAND_KEY, Perm::OPEN,
                       [](String) { return OtaService::getInstance().describeKey(); }));
    addCommand(Command("/key.set", "Store the deployment key: /key.set <64 hex digits>",
                       COMMAND_KEY_SET, Perm::LOCAL_ONLY,
                       [](String args) { return OtaService::getInstance().setKey(args); }));
    addCommand(Command("/maint.open",
                       "Open a maintenance window: /maint.open <seconds> [ap]. Without ap the "
                       "node joins its WiFi network and checks for updates; with ap it opens "
                       "its own access point for an upload",
                       COMMAND_MAINT_OPEN, Perm::SIGNED,
                       [](String args) { return OtaService::getInstance().openWindow(args); }));
    addCommand(Command("/maint.close", "Close the maintenance window", COMMAND_MAINT_CLOSE, Perm::SIGNED,
                       [](String) { return OtaService::getInstance().closeWindow(); }));
    addCommand(Command("/version",
                       "Show version, slot, image state, bootloader hash, board and address",
                       COMMAND_VERSION, Perm::OPEN,
                       [](String) { return OtaService::getInstance().version(); }));
    addCommand(Command("/ota.status", "Show the last update attempt and its outcome",
                       COMMAND_OTA_STATUS, Perm::OPEN,
                       [](String) { return OtaService::getInstance().otaStatus(); }));
    addCommand(Command("/reboot", "Restart the node", COMMAND_REBOOT, Perm::SIGNED,
                       [](String) { return OtaService::getInstance().reboot(); }));
    addCommand(Command("/ota.server",
                       "Show or set the OTA server: /ota.server [http://host:port/folder/|default]",
                       COMMAND_OTA_SERVER, Perm::LOCAL_ONLY,
                       [](String args) { return OtaService::getInstance().setServer(args); }));
    addCommand(Command("/maint.wifi",
                       "Store the WiFi credentials (encrypted by lmcmd.py wifi; on serial also "
                       "<ssid> [<password>])",
                       COMMAND_MAINT_WIFI, Perm::SIGNED,
                       [](String args) { return OtaService::getInstance().setWifi(args); }));
}
