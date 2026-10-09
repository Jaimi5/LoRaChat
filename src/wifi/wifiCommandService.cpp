#include "wifiCommandService.h"
#include "wifiServerService.h"

WiFiCommandService::WiFiCommandService() {
    addCommand(
        Command("/addSSID", "Add WiFi SSID", Perm::LOCAL_ONLY,
                [this](String args) { return WiFiServerService::getInstance().addSSID(args); }));

    addCommand(Command(
        "/addPassword", "Add WiFi Password", Perm::LOCAL_ONLY,
        [this](String args) { return WiFiServerService::getInstance().addPassword(args); }));

    addCommand(Command("/connectWiFi", "Connect WiFi", Perm::SIGNED,
                       [this](String args) {
                           bool result = WiFiServerService::getInstance().connectWiFi();
                           return result ? "Connected" : "Failed to connect";
                       }));

    addCommand(Command("/getIP", "Get IP of the device", Perm::OPEN,
                       [this](String args) { return WiFiServerService::getInstance().getIP(); }));

    addCommand(
        Command("/resetWiFiData", "Reset WiFi Data", Perm::SIGNED,
                [this](String args) { return WiFiServerService::getInstance().resetWiFiData(); }));
}