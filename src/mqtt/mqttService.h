#pragma once

#include <Arduino.h>

// #include <WiFiClientSecure.h>
#include <WiFiClient.h>

#include <MQTT.h>

#include <ArduinoLog.h>

#include "wifi/wifiServerService.h"

#include "mqttCommandService.h"

#include "message/messageService.h"

#include "message/messageManager.h"

#include "helpers/helper.h"

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

    void loop();

    bool isDeviceConnected();

    bool writeToMqtt(DataMessage* message);
    bool writeToMqtt(String message);

    // WiFiClientSecure net;
    WiFiClient net;

    MQTTClient client;

    MqttCommandService* mqttCommandService = new MqttCommandService();

    virtual void processReceivedMessage(messagePort port, DataMessage* message);

private:
    MqttService(): MessageService(appPort::MQTTApp, String("MQTT")) {
        commandService = mqttCommandService;
    };

    void createMqttTask();
    unsigned long lastMillis = 0;

    static void MqttLoop(void*);

    TaskHandle_t mqtt_TaskHandle = NULL;

    String localName = "";
};
