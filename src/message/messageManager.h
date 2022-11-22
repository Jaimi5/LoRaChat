#pragma once

#include <Arduino.h>

#include <ArduinoLog.h>

#include "dataMessage.h"

#include "messageService.h"

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

    void processReceivedMessage(ManagerMessage* message);

    void sendMessage(ManagerMessage* message);

    static void loopReceivedMessages(void*);

    static void loopSendMessages(void*);

    String getAvailableCommands();

    String executeCommand(uint8_t serviceId, String command);

    String executeCommand(String command);

private:

    //List of all apps available
    MessageService* services[10];

    TaskHandle_t sendMessageManager_TaskHandle = NULL;

    TaskHandle_t receiveMessageManager_TaskHandle = NULL;

    //TODO: Fix that to a specific sender
    static void sendMessageLoRaMesher(DataMessage* message) {};

    static void sendMessageBluetooth(DataMessage* message) {};

    static void sendMessageWiFi(DataMessage* message) {};

    void createTasks();
};