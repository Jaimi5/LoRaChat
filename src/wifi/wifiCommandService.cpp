#include "wifiCommandService.h"
#include "wifiServerService.h"

WiFiCommandService::WiFiCommandService() {
    addCommand(Command(F("/addSSID"), F("Add WiFi SSID"), WiFiMessageType::addSSID, 1,
        [this](String args) {
        return WiFiServerService::getInstance().addSSID(args);
    }));

    addCommand(Command(F("/addPassword"), F("Add WiFi Password"), WiFiMessageType::addPSD, 1,
        [this](String args) {
        return WiFiServerService::getInstance().addPassword(args);
    }));

    addCommand(Command(F("/saveWiFiData"), F("Save WiFi Data if device restarts"), WiFiMessageType::saveConfig, 1,
        [this](String args) {
        return WiFiServerService::getInstance().saveWiFiData();
    }));

    addCommand(Command(F("/connectWiFi"), F("Connect WiFi"), WiFiMessageType::connectWiFi, 1,
        [this](String args) {
        return WiFiServerService::getInstance().connectWiFi();
    }));

    addCommand(Command(F("/startServer"), F("Start WiFi Server"), WiFiMessageType::startServer, 1,
        [this](String args) {
        return WiFiServerService::getInstance().startServer();
    }));

    addCommand(Command(F("/stopServer"), F("Stop WiFi Server"), WiFiMessageType::stopServer, 1,
        [this](String args) {
        return WiFiServerService::getInstance().stopServer();
    }));
}