#include "wifiCommandService.h"
#include "wifiServerService.h"

WiFiCommandService::WiFiCommandService() {
    addCommand(
        Command("/addSSID", "Add WiFi SSID", WiFiMessageType::addSSID, Perm::LOCAL_ONLY,
                [this](String args) { return WiFiServerService::getInstance().addSSID(args); }));

    addCommand(Command(
        "/addPassword", "Add WiFi Password", WiFiMessageType::addPSD, Perm::LOCAL_ONLY,
        [this](String args) { return WiFiServerService::getInstance().addPassword(args); }));

    addCommand(Command("/connectWiFi", "Connect WiFi", WiFiMessageType::connectWiFi, Perm::SIGNED,
                       [this](String args) {
                           bool result = WiFiServerService::getInstance().connectWiFi();
                           return result ? "Connected" : "Failed to connect";
                       }));

    addCommand(Command("/getIP", "Get IP of the device", WiFiMessageType::getIP, Perm::OPEN,
                       [this](String args) { return WiFiServerService::getInstance().getIP(); }));

    addCommand(
        Command("/resetWiFiData", "Reset WiFi Data", WiFiMessageType::resetConfig, Perm::SIGNED,
                [this](String args) { return WiFiServerService::getInstance().resetWiFiData(); }));
}