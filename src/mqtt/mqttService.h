#pragma once

#include <Arduino.h>

#include "wifi/wifiServerService.h"

#include "mqttCommandService.h"

#include "message/messageService.h"

#include "message/messageManager.h"
#include "message/ownedQueue.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "config.h"

// TODO: Check for wake from sleep mode.
// TODO: Check for max characters in a message to avoid buffer overflow.

class MqttService : public MessageService {
public:
    static MqttService& getInstance() {
        static MqttService instance;
        return instance;
    }

    void initMqtt(String localName);

    bool isInitialized() { return initialized; }

    bool connect();

    void disconnect();

    bool isDeviceConnected();

    bool writeToMqtt(DataMessage* message);

    virtual void processReceivedMessage(messagePort port, DataMessage* message);

    void inline process_message(const char* topic, int topic_len, const char* payload,
                                int data_len);

    void processReceivedMessageFromMQTT(String& topic, String& payload);

    /** @return the topic this gateway takes text commands on: cmd/<address in hex>. */
    static String commandTopic();

    /** Publishes "<request id> <text>" on cmd-resp/<node in hex>. */
    void publishCommandReply(uint16_t node, uint8_t requestId, const String& text);

    String localName = "";

private:
    MqttService() : MessageService(appPort::MQTTApp, String("MQTT")) {
        commandService = &commands_;
    }

    MqttCommandService commands_;

    void createMqttTask();

    static void MqttLoop(void*);

    TaskHandle_t mqtt_TaskHandle = nullptr;

    /** A message received from the broker, waiting for the MQTT task. */
    struct MqttInbound {
        String topic;
        String body;
    };
    OwnedQueue<MqttInbound> receiveQueue_;

    void processMQTTMessage();

    void mqtt_service_init(const char* client_id);
    void mqtt_app_start(const char* client_id);
    void mqtt_service_send(const char* topic, const char* data, int len);

    bool initialized = false;
};
