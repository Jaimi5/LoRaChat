#include "wifiCommandService.h"
#include "wifiServerService.h"

WiFiCommandService::WiFiCommandService() {
    addCommand(Command("/addSSID", "Set the WiFi network name until the next restart",
                       Perm::LOCAL_ONLY,
                       [](String args) { return WiFiServerService::getInstance().addSSID(args); })
                   .withUsage("<ssid of 1 to 32 bytes>"));

    addCommand(
        Command("/addPassword", "Set the WiFi password until the next restart", Perm::LOCAL_ONLY,
                [](String args) { return WiFiServerService::getInstance().addPassword(args); })
            .withUsage("<password of at most 64 bytes>"));

    addCommand(Command("/connectWiFi", "Connect WiFi", Perm::SIGNED, [](String) -> CommandReply {
                   if (WiFiServerService::getInstance().connectWiFi()) return "Connected";
                   return CommandReply(WIFI_ERR_FAILED, "");
               }).withCode(WIFI_ERR_FAILED, "The WiFi did not connect"));

    addCommand(Command("/getIP", "Get IP of the device", Perm::OPEN, [](String) {
                   return WiFiServerService::getInstance().getIP();
               }).withCode(WIFI_ERR_FAILED, "The node has no IP address"));

    addCommand(Command("/resetWiFiData", "Reset WiFi Data", Perm::SIGNED, [](String) {
                   return WiFiServerService::getInstance().resetWiFiData();
               }).withCode(WIFI_ERR_FAILED, "The WiFi settings could not be reset"));
}
