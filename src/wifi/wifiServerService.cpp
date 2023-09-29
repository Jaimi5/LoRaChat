#include "wifiServerService.h"
#include "wifiServerServiceNew.cpp"


void WiFiServerService::initWiFi() {
    // // Create the semaphore with a count of 1
    // wifiSemaphore = xSemaphoreCreateBinary();
    // if (wifiSemaphore == NULL) {
    //     Log.errorln(F("Error creating WiFi semaphore"));
    // }
    // xSemaphoreGive(wifiSemaphore);

    wifi_init_sta();

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
    if (!isWifiConnected()) {
        connectWiFi();
        if (!isWifiConnected())
            return false;
    }

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
    configService.setConfig("WiFiSSid", this->ssid);
    configService.setConfig("WiFiPsw", this->password);

    return F("WiFi data saved");
}

String WiFiServerService::resetWiFiData() {
    ConfigService& configService = ConfigService::getInstance();
    configService.setConfig("WiFiSSid", DEFAULT_WIFI_SSID);
    configService.setConfig("WiFiPsw", DEFAULT_WIFI_PASSWORD);

    this->ssid = DEFAULT_WIFI_SSID;
    this->password = DEFAULT_WIFI_PASSWORD;

    return F("WiFi data reset");
}

bool WiFiServerService::isWifiConnected() {
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
        // Connected to an AP
        printf("Connected to AP with SSID: %s\n", ap_info.ssid);
        // if (ap_info.ssid == this->ssid.c_str())
        //     return true;
        return true;
    }
    else if (ret == ESP_ERR_WIFI_CONN) {
        // Not connected to an AP
        printf("Not connected to an AP\n");
    }
    else {
        // Other error
        printf("Failed to get AP info: %s\n", esp_err_to_name(ret));
    }
    return false;
}


bool WiFiServerService::connectWiFi() {
    Log.verboseln(F("Connecting to WiFi..."));

    if (isWifiConnected()) {
        Log.verboseln(F("WiFi already connected"));
        LoRaMeshService::getInstance().setGateway();
        return true;
    }

    wifi_connect(this->ssid.c_str(), this->password.c_str());

    if (isWifiConnected()) {
        LoRaMeshService::getInstance().setGateway();
        return true;
    }

    return false;

    // // Wait for the WiFi semaphore
    // if (xSemaphoreTake(wifiSemaphore, portMAX_DELAY) == pdTRUE) {
    //     if (this->ssid == DEFAULT_WIFI_SSID || this->password == DEFAULT_WIFI_PASSWORD) {
    //         xSemaphoreGive(wifiSemaphore);
    //         return F("WiFi not configured");
    //     }

    //     // Check if already connected to the desired WiFi network
    //     if (isWifiConnected()) {
    //         LoRaMeshService::getInstance().setGateway();
    //         xSemaphoreGive(wifiSemaphore);
    //         return F("WiFi already connected");
    //     }

    //     Log.verbose(F("Trying to connect to WiFi network %s with pwd: %s!"), this->ssid.c_str(), this->password.c_str());

    //     WiFi.begin(this->ssid.c_str(), this->password.c_str());
    //     int i = 0;
    //     while (i < MAX_CONNECTION_TRY) {
    //         vTaskDelay(500 / portTICK_PERIOD_MS);
    //         Log.verbose(F("."));
    //         if (isWifiConnected()) {
    //             LoRaMeshService::getInstance().setGateway();
    //             // Release the WiFi semaphore
    //             xSemaphoreGive(wifiSemaphore);
    //             return F("WiFi connected");
    //         }
    //         else if (WiFi.status() == WL_CONNECT_FAILED || WiFi.status() == WL_NO_SSID_AVAIL) {
    //             // Release the WiFi semaphore
    //             xSemaphoreGive(wifiSemaphore);
    //             return F("WiFi connection failed");
    //         }
    //         i++;
    //     }

    //     // Release the WiFi semaphore
    //     xSemaphoreGive(wifiSemaphore);
    // }

    // LoRaMeshService::getInstance().removeGateway();
    // return F("WiFi connection timeout");

}

String WiFiServerService::disconnectWiFi() {
    esp_wifi_stop();
    // while (xSemaphoreTake(wifiSemaphore, portMAX_DELAY) != pdTRUE) {
    //     vTaskDelay(100 / portTICK_PERIOD_MS);
    // }

    Log.verboseln(F("Disconnecting from WiFi"));
    // WiFi.disconnect();
    LoRaMeshService::getInstance().removeGateway();

    // xSemaphoreGive(wifiSemaphore);

    return F("WiFi disconnected");
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

#ifdef SIMULATION_ENABLED
    if (LoraMesher::getInstance().getLocalAddress() != WIFI_ADDR_CONNECTED)
        return false;
#endif

    this->ssid = configService.getConfig("WiFiSSid", DEFAULT_WIFI_SSID);
    this->password = configService.getConfig("WiFiPsw", DEFAULT_WIFI_PASSWORD);

    //TODO: Remove this when we have a way to set the default wifi data
    if (this->ssid == DEFAULT_WIFI_SSID && this->password == DEFAULT_WIFI_PASSWORD) {
        this->ssid = WIFI_SSID;
        this->password = WIFI_PASSWORD;
    }

    return true;
}
