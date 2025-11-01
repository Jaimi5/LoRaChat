#pragma once

#include "LoraMesher.h"

#include "wifiCommandService.h"

#include "message/messageManager.h"

#include "message/messageService.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "config.h"

#define DEFAULT_WIFI_SSID "DEFAULT_SSID"
#define DEFAULT_WIFI_PASSWORD "DEFAULT_PASSWORD"


class WiFiServerService : public MessageService {
public:
    /**
     * @brief Construct a new WiFiServerService object
     *
     */
    static WiFiServerService& getInstance() {
        static WiFiServerService instance;
        return instance;
    }

    ~WiFiServerService() {
        if (wiFiCommandService != nullptr) {
            delete wiFiCommandService;
        }
    }

    void initWiFi();

    bool connectWiFi();

    bool disconnectWiFi();

    bool isConnected();


    String addSSID(String ssid);

    String addPassword(String password);

    String resetWiFiData();


    String getIP();

    String getSSID();

    String getPassword();


    WiFiCommandService* wiFiCommandService = nullptr;

    virtual void processReceivedMessage(messagePort port, DataMessage* message);

    void sendMessage(DataMessage* message);


private:
    WiFiServerService() : MessageService(appPort::WiFiApp, String("WiFi")) {
        wiFiCommandService = new WiFiCommandService();
        commandService = wiFiCommandService;
    };

    String ssid = DEFAULT_WIFI_SSID;
    String password = DEFAULT_WIFI_PASSWORD;

    void wifi_init_sta();

    bool restartWiFiData();

    bool checkIfWiFiCredentialsAreSet();

    TaskHandle_t wifi_TaskHandle = NULL;

    void createWiFiTask();

    static void wifi_task(void*);

    bool connected = false;

    bool initialized = false;

    bool addWiFiCredentialsFromConfig();
};