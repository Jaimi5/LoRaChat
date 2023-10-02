#pragma once

#include <Arduino.h>

#include <vector>

#include "dataMessage.h"

#include "messageService.h"

#include "loramesh/loraMeshService.h"

#include "mqtt/mqttService.h"

#include "wifi/wifiServerService.h"

class MessageManager {
public:
    /**
     * @brief Construct a new MessageManager object
     *
     */
    static MessageManager& getInstance() {
        static MessageManager instance;
        return instance;
    }

    xQueueHandle xProcessQueue;

    xQueueHandle xSendQueue;

    void init();

    void addMessageService(MessageService* service);

    void processReceivedMessage(messagePort port, DataMessage* message);

    void sendMessage(messagePort port, DataMessage* message);

    String getAvailableCommands();

    String executeCommand(uint8_t serviceId, uint8_t commandId, String args);

    String executeCommand(uint8_t serviceId, String command);

    String executeCommand(String command);

    String getJSON(DataMessage* message);

    DataMessage* getDataMessage(String json);

    String printDataMessageHeader(String title, DataMessage* message);

private:

    MessageManager() {};

    std::vector<MessageService*> services;

    TaskHandle_t sendMessageManager_TaskHandle = NULL;

    TaskHandle_t receiveMessageManager_TaskHandle = NULL;

    //TODO: Fix that to a specific sender
    static void sendMessageLoRaMesher(DataMessage* message);

    static void sendMessageBluetooth(DataMessage* message) {};

    static void sendMessageWiFi(DataMessage* message);
    static void sendMessageMqtt(DataMessage* message);
};