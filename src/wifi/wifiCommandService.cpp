#include "wifiCommandService.h"
#include "wifiServerService.h"

WiFiCommandService::WiFiCommandService() {
    addCommand(Command("/addSSID", "Add WiFi SSID", WiFiMessageType::addSSID, 1,
        [this](String args) {
        return WiFiServerService::getInstance().addSSID(args);
    }));

    addCommand(Command("/addPassword", "Add WiFi Password", WiFiMessageType::addPSD, 1,
        [this](String args) {
        return WiFiServerService::getInstance().addPassword(args);
    }));

    addCommand(Command("/connectWiFi", "Connect WiFi", WiFiMessageType::connectWiFi, 1,
        [this](String args) {
        bool result = WiFiServerService::getInstance().connectWiFi();
        return result ? "Connected" : "Failed to connect";
    }));

    addCommand(Command("/getIP", "Get IP of the device", WiFiMessageType::getIP, 1,
        [this](String args) {
        return WiFiServerService::getInstance().getIP();
    }));

    addCommand(Command("/resetWiFiData", "Reset WiFi Data", WiFiMessageType::resetConfig, 1,
        [this](String args) {
        return WiFiServerService::getInstance().resetWiFiData();
    }));
}