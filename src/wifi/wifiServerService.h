#pragma once

#include <Arduino.h>

#include <ArduinoLog.h>

#include "./config.h"

#include "./helpers/helper.h"

// Load Wi-Fi library
#include <WiFi.h>

#include "wifiCommandService.h"

#include "./configuration/configService.h"

#include "./message/messageManager.h"

#include "./message/messageService.h"

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

    WiFiServer* server = new WiFiServer(80);

    WiFiCommandService* wiFiCommandService = new WiFiCommandService();

    virtual void processReceivedMessage(messagePort port, DataMessage* message);

    String addSSID(String ssid);

    String addPassword(String password);

    String saveWiFiData();

    String connectWiFi();

    String initWiFiServer();

    String startServer();

    String stopServer();

    bool serverAvailable = false;

private:

    WiFiServerService(): MessageService(appPort::WiFiApp, String("WiFi")) {
        commandService = wiFiCommandService;
    };

    String ssid = "DEFAULT_SSID";
    String password = "DEFAULT_PASSWORD";

    TaskHandle_t server_TaskHandle = NULL;

    static void ServerLoop(void*);

    void createServerTask();

    bool restartWiFiData();
};