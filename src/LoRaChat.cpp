#include "config.h"

#include <Arduino.h>
#include "ArduinoLog.h"
#include "display.h"

#include "helpers/helper.h"

#include "message/messageManager.h"
#include "loramesh/loraMeshService.h"
#include "mqtt/mqttService.h"
#include "wifi/wifiServerService.h"

#include "commands/commandService.h"

class LC {

    private:
        LoRaMeshService& loraMeshService = LoRaMeshService::getInstance();
        WiFiServerService& wiFiService = WiFiServerService::getInstance();
        MqttService& mqttService = MqttService::getInstance();
        MessageManager& manager = MessageManager::getInstance();

        void initManager() {
            manager.init();
            manager.addMessageService(&loraMeshService);
            manager.addMessageService(&wiFiService);
            manager.addMessageService(&mqttService);
        }

        void initWiFi(String wifi_ssid, String wifi_password) {
            wiFiService.initWiFi(wifi_ssid, wifi_password);
        }

        void initLoRaMesher() {
            loraMeshService.initLoraMesherService();
        }

        void initMqtt(String mqtt_server, uint mqtt_port, String mqtt_username, String mqtt_password) {
            mqttService.initMqtt(String(loraMeshService.getLocalAddress()), mqtt_server, mqtt_port, mqtt_username, mqtt_password);
        }

        void initDisplay() {
            Screen.initDisplay();
            displayStatus();
        }

    public:
            void init(String wifi_ssid, String wifi_password, String mqtt_server, uint mqtt_port, String mqtt_username, String mqtt_password) {
            initManager();
            initWiFi(wifi_ssid, wifi_password);
            initLoRaMesher();
            // Give some time for the WiFi to connect
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            initMqtt(mqtt_server, mqtt_port, mqtt_username, mqtt_password);
            initDisplay();
        }

        void registerApplication(MessageService *app) {
            manager.addMessageService(app);
        }

        void displayStatus() {
            char lineThree[25];
            String lineTwo = String(loraMeshService.getLocalAddress()) + " | " + wiFiService.getIP();
            sprintf(lineThree, "Free ram: %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            Screen.changeLineTwo(lineTwo);
            Screen.changeLineThree(lineThree);
            Screen.drawDisplay();
        }

        void printCommands() {
            Serial.println(manager.getAvailableCommands());
        }
};
