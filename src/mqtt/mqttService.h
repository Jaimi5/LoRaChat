#pragma once

#include <Arduino.h>

#include "wifi/wifiServerService.h"

#include "mqttCommandService.h"

#include "message/messageService.h"

#include "message/messageManager.h"

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "config.h"

// TODO: Check for wake from sleep mode.
// TODO: Check for max characters in a message to avoid buffer overflow.

class MqttService: public MessageService {
public:
    /**
     * @brief Construct a new BluetoothService object
     *
     */
    static MqttService& getInstance() {
        static MqttService instance;
        return instance;
    }

    void initMqtt(String localName);

    bool isInitialized() {
        return initialized;
    }

    bool connect();

    void disconnect();

    bool isDeviceConnected();

    bool writeToMqtt(DataMessage* message);
    bool writeToMqtt(String message);

    MqttCommandService* mqttCommandService = new MqttCommandService();

    virtual void processReceivedMessage(messagePort port, DataMessage* message);

    void inline process_message(const char* topic, const char* payload);

    void processReceivedMessageFromMQTT(String& topic, String& payload);

    void mqtt_service_subscribe(const char* topic);

    String localName = "";

private:
    MqttService(): MessageService(appPort::MQTTApp, String("MQTT")) {
        commandService = mqttCommandService;
    };

    void createMqttTask();

    static void MqttLoop(void*);

    TaskHandle_t mqtt_TaskHandle = NULL;

    struct MQTTQueueMessageV2 {
        String topic;
        String body;
    };
    QueueHandle_t receiveQueue;
    MQTTQueueMessageV2* mqttMessageReceiveV2;

    bool sendMqttMessage(MQTTQueueMessageV2* message);


    void processMQTTMessage();

    void mqtt_service_init(const char* client_id);
    void mqtt_app_start(const char* client_id);
    void mqtt_service_send(const char* topic, const char* data, int len);

    bool initialized = false;
};
