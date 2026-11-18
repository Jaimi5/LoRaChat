#include "otaCommandService.h"

#include "maintWifi.h"
#include "otaService.h"
#include "wifi/wifiServerService.h"

OtaCommandService::OtaCommandService() {
    addCommand(Command("/key", "Show whether a deployment key is stored, and its fingerprint",
                       Perm::OPEN,
                       [](String) { return OtaService::getInstance().describeKey(); }));
    addCommand(Command("/key.set", "Store the deployment key", Perm::LOCAL_ONLY,
                       [](String args) { return OtaService::getInstance().setKey(args); })
                   .withUsage("<64 hex digits>")
                   .withCode(OTA_ERR_FAILED, "The key could not be stored"));
    addCommand(
        Command("/maint.open",
                "Open a maintenance window. Without ap the node joins its WiFi network and "
                "checks for updates. With ap it opens its own access point for an upload",
                Perm::SIGNED,
                [](String args) { return OtaService::getInstance().openWindow(args); })
            .withUsage(String("<seconds> [ap] (up to ") + MaintWindowPlan::MAX_PULL_S + ", or " +
                       MaintWindowPlan::MAX_AP_S + " with ap)")
            .withCode(MAINT_ERR_GATEWAY_AP,
                      "Gateways keep their own WiFi. The access point is for sensors")
            .withCode(MAINT_ERR_VERIFYING, "The new image is still being verified. Try again later")
            .withCode(MAINT_ERR_NO_KEY, "The node has no deployment key")
            .withCode(MAINT_ERR_POLICY, "Refused, battery or memory too low")
            .withCode(MAINT_ERR_DAILY_LIMIT, "Too many windows in the last 24 h")
            .withCode(MAINT_ERR_STILL_OPEN, "The open window did not close")
            .withCode(MAINT_ERR_NO_TASK, "The window could not be started")
            .withCode(MAINT_ERR_GATEWAY_WIFI, "The gateway WiFi is not connected")
            .withCode(MAINT_ERR_CHECK_RUNNING, "An update check is already running"));
    addCommand(Command("/maint.close", "Close the maintenance window", Perm::SIGNED,
                       [](String) { return OtaService::getInstance().closeWindow(); }));
    addCommand(Command("/version",
                       "Show version, slot, image state, bootloader hash, board and address",
                       Perm::OPEN,
                       [](String) { return OtaService::getInstance().version(); }));
    addCommand(Command("/ota.status", "Show the last update attempt and its outcome",
                       Perm::OPEN,
                       [](String) { return OtaService::getInstance().otaStatus(); }));
    addCommand(Command("/reboot", "Restart the node", Perm::SIGNED, [](String) {
                   return OtaService::getInstance().reboot();
               }).withCode(OTA_ERR_FAILED, "The restart could not be scheduled"));
    addCommand(Command("/ota.server", "Show or set the OTA server", Perm::LOCAL_ONLY,
                       [](String args) { return OtaService::getInstance().setServer(args); })
                   .withUsage("[http://host:port/folder/ or default]")
                   .withCode(OTA_ERR_FAILED, "The server could not be stored"));
    addCommand(Command("/maint.wifi", "Store the WiFi credentials", Perm::SIGNED,
                       [](String args) { return OtaService::getInstance().setWifi(args); })
                   .withUsage("<credentials encrypted by lmcmd.py wifi>, or on serial <ssid> "
                              "[<password of 8 to 63 characters>]")
                   .withCode(WIFI_ERR_INVALID, "The WiFi credentials are not valid")
                   .withCode(WIFI_ERR_WINDOW_OPEN, "A maintenance window is open. Close it first")
                   .withCode(WIFI_ERR_STORE, "The WiFi credentials could not be stored"));
}
