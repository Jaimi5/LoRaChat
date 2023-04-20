#pragma once

#include <Arduino.h>

#include <ArduinoLog.h>

#include "config.h"

#include "LoraMesher.h"

#include "helpers/helper.h"

// Load Wi-Fi library
#include <WiFi.h>

#include "wifiCommandService.h"

#include "configuration/configService.h"

#include "message/messageManager.h"

#include "message/messageService.h"

#define DEFAULT_WIFI_SSID "DEFAULT_SSID"
#define DEFAULT_WIFI_PASSWORD "DEFAULT_PASSWORD"

class WiFiServerService: public MessageService {
public:

    /**
     * @brief Construct a new WiFiServerService object
     *
     */
    static WiFiServerService& getInstance() {
        static WiFiServerService instance;
        return instance;
    }

    void initWiFi();

    WiFiCommandService* wiFiCommandService = new WiFiCommandService();

    virtual void processReceivedMessage(messagePort port, DataMessage* message);

    void sendMessage(DataMessage* message);

    bool connectAndSend(DataMessage* message);

    String addSSID(String ssid);

    String addPassword(String password);

    String saveWiFiData();

    String resetWiFiData();

    String connectWiFi();

    String getIP();

    String getSSID();

    String getPassword();

    void responseCommand(WiFiClient client, String header);

private:

    WiFiServerService(): MessageService(appPort::WiFiApp, String("WiFi")) {
        commandService = wiFiCommandService;
    };

    String ssid = WIFI_SSID;
    String password = WIFI_PASSWORD;

    bool restartWiFiData();
};