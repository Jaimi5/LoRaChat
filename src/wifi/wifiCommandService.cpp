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

    addCommand(Command(0x50, "/connectWiFi", "Connect WiFi", Perm::SIGNED)
                   .reply("", "Connected")
                   .run([](const Fields&, Fields&) -> TypedResult {
                       if (WiFiServerService::getInstance().connectWiFi()) return TypedResult();
                       return TypedResult(WIFI_ERR_FAILED);
                   })
                   .withCode(WIFI_ERR_FAILED, "The WiFi did not connect"));

    addCommand(Command(0x51, "/getIP", "Get IP of the device", Perm::OPEN)
                   .reply("a:u b:u c:u d:u", "{0}.{1}.{2}.{3}")
                   .run([](const Fields&, Fields& out) {
                       return WiFiServerService::getInstance().ipInto(out);
                   })
                   .withCode(WIFI_ERR_FAILED, "The node has no IP address"));

    addCommand(Command(0x52, "/resetWiFiData", "Reset WiFi Data", Perm::SIGNED)
                   .reply("", "WiFi data reset successfully")
                   .run([](const Fields&, Fields&) {
                       return WiFiServerService::getInstance().resetWiFiData();
                   })
                   .withCode(WIFI_ERR_FAILED, "The WiFi settings could not be reset", "x"));
}
