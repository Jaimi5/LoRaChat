#include "wifiServerService.h"

void WiFiServerService::initWiFi() {
    if (restartWiFiData())
        connectWiFi();

    if (WiFi.status() == WL_CONNECTED)
        Log.verboseln("WiFi connected: %s", WiFi.localIP().toString().c_str());

    Log.verboseln(F("WiFi initialized"));
}

void WiFiServerService::processReceivedMessage(messagePort port, DataMessage* message) {
    if (connectAndSend(message))
        Log.verboseln(F("Message sent to WiFi"));
    else
        Log.errorln(F("Error sending message to WiFi"));
}

void WiFiServerService::sendMessage(DataMessage* message) {

}

bool WiFiServerService::connectAndSend(DataMessage* message) {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED)
            return false;
    }

    //TODO: Check last time connected and if it's too long ago, try to send it lora mesh

    String json = MessageManager::getInstance().getJSON(message);
    return false;
    // return HTTPService::sendHTTPPOST(json);
}

String WiFiServerService::addSSID(String ssid) {
    this->ssid = ssid;

    return F("SSID added");
}

String WiFiServerService::addPassword(String password) {
    this->password = password;

    return F("Password added");
}

String WiFiServerService::saveWiFiData() {
    ConfigService& configService = ConfigService::getInstance();
    configService.setConfig("WiFiSSid", ssid);
    configService.setConfig("WiFiPsw", password);

    return F("WiFi data saved");
}

String WiFiServerService::resetWiFiData() {
    ConfigService& configService = ConfigService::getInstance();
    configService.setConfig("WiFiSSid", DEFAULT_WIFI_SSID);
    configService.setConfig("WiFiPsw", DEFAULT_WIFI_PASSWORD);

    ssid = DEFAULT_WIFI_SSID;
    password = DEFAULT_WIFI_PASSWORD;

    return F("WiFi data reset");
}

String WiFiServerService::connectWiFi() {
    if (ssid == DEFAULT_WIFI_SSID || password == DEFAULT_WIFI_PASSWORD)
        return F("WiFi not configured");

    WiFi.scanNetworks();
    Log.verbose(F("Trying to connect to WiFi network %s with pwd: %s!"), ssid.c_str(), password.c_str());

    WiFi.begin(ssid.c_str(), password.c_str());
    int i = 0;
    while (WiFi.status() != WL_CONNECTED && i < MAX_CONNECTION_TRY) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        Log.verbose(F("."));
        i++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        LoRaMeshService::getInstance().setGateway();
    }
    else
        LoRaMeshService::getInstance().removeGateway();

    return WiFi.status() == WL_CONNECTED ? F("WiFi connected") : F("WiFi connection failed");
}

String WiFiServerService::getIP() {
    return WiFi.localIP().toString();
}

String WiFiServerService::getSSID() {
    ConfigService& configService = ConfigService::getInstance();
    return configService.getConfig("WiFiSSid", DEFAULT_WIFI_SSID);
}
String WiFiServerService::getPassword() {
    ConfigService& configService = ConfigService::getInstance();
    return configService.getConfig("WiFiPsw", DEFAULT_WIFI_SSID);
}

bool WiFiServerService::restartWiFiData() {
    ConfigService& configService = ConfigService::getInstance();
    ssid = configService.getConfig("WiFiSSid", DEFAULT_WIFI_SSID);
    password = configService.getConfig("WiFiPsw", DEFAULT_WIFI_PASSWORD);

    //TODO: Remove this when we have a way to set the default wifi data
    if (ssid == DEFAULT_WIFI_SSID && password == DEFAULT_WIFI_PASSWORD) {
        ssid = WIFI_SSID;
        password = WIFI_PASSWORD;
    }

    return true;
}
