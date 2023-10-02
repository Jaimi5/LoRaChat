#include "wifiServerService.h"

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char* TAG = "WiFi";

static int s_retry_num = 0;

void WiFiServerService::initWiFi() {
    wifi_init_sta();

    if (restartWiFiData())
        connectWiFi();

    createWiFiTask();

    vTaskDelay(20000 / portTICK_PERIOD_MS); // Wait 20 seconds

    ESP_LOGI(TAG, "WiFi initialized");
}

void WiFiServerService::createWiFiTask() {
    int res = xTaskCreate(
        wifi_task,
        "WiFi Task",
        2048,
        (void*) 1,
        2,
        &wifi_TaskHandle);
    if (res != pdPASS)
        ESP_LOGE(TAG, "WiFi task handle error: %d", res);
}

void WiFiServerService::wifi_task(void*) {
    ESP_LOGV(TAG, "WiFi loop started");
    WiFiServerService& wiFiServerService = WiFiServerService::getInstance();
    LoRaMeshService& LoRaMeshService = LoRaMeshService::getInstance();

    for (;;) {
        /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
         * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);

        ESP_LOGI(TAG, "Bit received: %d", bits);

        /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
         * happened. */
        if (bits & WIFI_CONNECTED_BIT == WIFI_CONNECTED_BIT) {
            LoRaMeshService.setGateway();
            wiFiServerService.connected = true;
            ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", wiFiServerService.ssid, wiFiServerService.password);
        }
        else if (bits & WIFI_FAIL_BIT == WIFI_FAIL_BIT) {
            wiFiServerService.connected = false;
            LoRaMeshService.removeGateway();
            ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", wiFiServerService.ssid, wiFiServerService.password);
        }

        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_CONNECTION_TRY) {
            esp_wifi_connect();
            s_retry_num++;
            // ESP_LOGI(TAG,"retry to connect to the AP");
        }
        else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        // ESP_LOGI(TAG,"connect to the AP fail");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        // ESP_LOGI(TAG,"got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void WiFiServerService::wifi_init_sta() {
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL,
        &instance_got_ip));
}

void WiFiServerService::processReceivedMessage(messagePort port, DataMessage* message) {
    // if (connectAndSend(message))
    //     ESP_LOGI(TAG,"Message sent to WiFi"));
    // else
    // LOG NOT IMPLEMENTED
    ESP_LOGW(TAG, "Message not sent to WiFi");
}

void WiFiServerService::sendMessage(DataMessage* message) {

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

bool WiFiServerService::isConnected() {
    return connected;
}

bool WiFiServerService::connectWiFi() {
    if (isConnected())
        return true;

    if (!checkIfWiFiCredentialsAreSet()) {
        ESP_LOGI(TAG, "WiFi credentials are not set");
        return false;
    }

    ESP_LOGI(TAG, "Connecting to %s...", ssid);

    wifi_config_t wifi_config = {0};  // initialize all fields to zero

    // Assume ssidHelp and passwordHelp are null-terminated strings
    // and their lengths are less than the size of .ssid and .password arrays
    memcpy(wifi_config.sta.ssid, ssid.c_str(), ssid.length());
    memcpy(wifi_config.sta.password, password.c_str(), password.length());

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi...");

    return true;
}

bool WiFiServerService::disconnectWiFi() {
    esp_wifi_stop();

    ESP_LOGI(TAG, "Disconnecting from WiFi");
    LoRaMeshService::getInstance().removeGateway();

    connected = false;

    return true;
}

String WiFiServerService::getIP() {
    //TODO: Check if this is the correct way to get the IP

    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        printf("Could not get netif handle\n");
        return F("No IP");
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
    if (ret == ESP_OK) {
        char ipStr[16];  // Buffer to hold the IP address string
        ip4addr_ntoa_r((const ip4_addr_t*) &ip_info.ip, ipStr, sizeof(ipStr));
        return String(ipStr);
    }

    return F("No IP");
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

    // #ifdef SIMULATION_ENABLED
    //     if (LoraMesher::getInstance().getLocalAddress() != WIFI_ADDR_CONNECTED)
    //         return false;
    // #endif

    // TODO: CHANGE THIS TO THE CORRECT DEFAULT VALUES

    this->ssid = WIFI_SSID;
    this->password = WIFI_PASSWORD;

    // this->ssid = configService.getConfig("WiFiSSid", DEFAULT_WIFI_SSID);
    // this->password = configService.getConfig("WiFiPsw", DEFAULT_WIFI_PASSWORD);

    // //TODO: Remove this when we have a way to set the default wifi data
    // if (this->ssid == DEFAULT_WIFI_SSID && this->password == DEFAULT_WIFI_PASSWORD) {

    // }

    return true;
}

bool WiFiServerService::checkIfWiFiCredentialsAreSet() {
    return this->ssid != DEFAULT_WIFI_SSID && this->password != DEFAULT_WIFI_PASSWORD;
}
