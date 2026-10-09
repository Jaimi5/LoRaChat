#pragma once

#include "loramesh/loraMeshService.h"

#include "wifiCommandService.h"

#include "message/messageManager.h"

#include "message/messageService.h"

#include <string.h>

#include <mutex>
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

#include "connectBackoff.h"

#include <atomic>

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

    /** @return true once the station has an IP address (it can reach the network). */
    bool hasIp();


    static constexpr size_t MAX_SSID_LENGTH = sizeof(wifi_sta_config_t::ssid);
    static constexpr size_t MAX_PASSWORD_LENGTH = sizeof(wifi_sta_config_t::password);

    /** Sets the SSID used by the next connect. @return a message; rejects empty or > 32 bytes. */
    String addSSID(String ssid);

    /** Sets the password used by the next connect. @return a message; rejects > 64 bytes. */
    String addPassword(String password);

    /**
     * @brief Saves the station credentials in the WiFi driver's NVS config and reconnects
     *        with them. @return the reply to the command.
     */
    String storeCredentials(const String& ssid, const String& password);

    String resetWiFiData();


    String getIP();

    String getSSID();


    WiFiCommandService* wiFiCommandService = nullptr;

    virtual void processReceivedMessage(messagePort port, DataMessage* message);

    void sendMessage(DataMessage* message);


private:
    WiFiServerService() : MessageService(appPort::WiFiApp, String("WiFi")) {
        wiFiCommandService = new WiFiCommandService();
        commandService = wiFiCommandService;
    };

    /** Guards the credentials and the connection state shared by the WiFi task and commands. */
    std::recursive_mutex stateMutex_;
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

    bool wifiStarted = false;

    // True from the start of a connection attempt until the driver reports success or failure.
    std::atomic<bool> connecting{false};

    static constexpr uint32_t INITIAL_CONNECT_BACKOFF_MS = 5000;
    static constexpr uint32_t MAX_CONNECT_BACKOFF_MS = 300000;
    ConnectBackoff connectBackoff{INITIAL_CONNECT_BACKOFF_MS, MAX_CONNECT_BACKOFF_MS};

    bool addWiFiCredentialsFromConfig();
};