#include "otaCommandService.h"

#include "otaService.h"

OtaCommandService::OtaCommandService() {
    addCommand(Command("/key", "Show whether a deployment key is stored, and its fingerprint",
                       Perm::OPEN,
                       [](String) { return OtaService::getInstance().describeKey(); }));
    addCommand(Command("/key.set", "Store the deployment key: /key.set <64 hex digits>",
                       Perm::LOCAL_ONLY,
                       [](String args) { return OtaService::getInstance().setKey(args); }));
    addCommand(Command("/maint.open",
                       "Open a maintenance window: /maint.open <seconds> [ap]. Without ap the "
                       "node joins its WiFi network and checks for updates; with ap it opens "
                       "its own access point for an upload",
                       Perm::SIGNED,
                       [](String args) { return OtaService::getInstance().openWindow(args); }));
    addCommand(Command("/maint.close", "Close the maintenance window", Perm::SIGNED,
                       [](String) { return OtaService::getInstance().closeWindow(); }));
    addCommand(Command("/version",
                       "Show version, slot, image state, bootloader hash, board and address",
                       Perm::OPEN,
                       [](String) { return OtaService::getInstance().version(); }));
    addCommand(Command("/ota.status", "Show the last update attempt and its outcome",
                       Perm::OPEN,
                       [](String) { return OtaService::getInstance().otaStatus(); }));
    addCommand(Command("/reboot", "Restart the node", Perm::SIGNED,
                       [](String) { return OtaService::getInstance().reboot(); }));
    addCommand(Command("/ota.server",
                       "Show or set the OTA server: /ota.server [http://host:port/folder/|default]",
                       Perm::LOCAL_ONLY,
                       [](String args) { return OtaService::getInstance().setServer(args); }));
    addCommand(Command("/maint.wifi",
                       "Store the WiFi credentials (encrypted by lmcmd.py wifi; on serial also "
                       "<ssid> [<password>])",
                       Perm::SIGNED,
                       [](String args) { return OtaService::getInstance().setWifi(args); }));
}
