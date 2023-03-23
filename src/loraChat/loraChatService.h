#pragma once

#include <Arduino.h>

#include <LoraMesher.h>

#include "config.h"

#include "loraChat.h"

#include "loraChatMessage.h"

#include "loraChatCommandService.h"

#include "message/messageService.h"

#include "message/messageManager.h"

#include "gps/gpsService.h"

#include "bluetooth/bluetoothService.h"

#include "time/timeHelper.h"

#include "wifi/httpService.h"

//TODO: Add contact service (or bluetooth), only ask for contact info to thus that have bluetooth port open

class LoRaChatService: public MessageService {
public:
    /**
     * @brief Construct a new loraChatService object
     *
     */
    static LoRaChatService& getInstance() {
        static LoRaChatService instance;
        return instance;
    }

    void initLoRaChatService();

    void addContact(DataMessage* message);

    void addContact(String name, uint16_t src);

    String getNameContact(uint16_t addr);

    uint16_t getAddrContact(String name);

    String getContactsString();

    String changeName(String newName);

    String requestGPS(messagePort port, String dst);

    String responseGPS(messagePort port, DataMessage* message);

    String startChatTo(String name);

    String chatTo(String args);

    String receiveChatMessage(messagePort port, DataMessage* message);

    String ackChatMessage(messagePort port, DataMessage* message);

    String receivedChatMessage(DataMessage* message);

    String findContacts();

    String getPreviousMessages();

    String sendChatToWifi(String args);

    LoRaChatMessage* createLoRaChatMessage();

    LoRaChatMessage* createLoRaChatMessage(String message);

    void requestContactInfo(messagePort port, uint16_t dst);

    void responseContactInfo(messagePort port, LoRaChatMessage* message);

    virtual void processReceivedMessage(messagePort port, DataMessage* message);

    LoRaChatCommandService* loraChatCommandService = new LoRaChatCommandService();

private:

    LoRaChatService(): MessageService(appPort::LoRaChat, String("LoRaChat")) {
        commandService = loraChatCommandService;
    };

    uint8_t requestId;

    uint16_t chatAddr;

    char myName[MAX_NAME_LENGTH] = "";

    LM_LinkedList<Info>* contactsList = new LM_LinkedList<Info>();

    PreviousMessage* previousMessage[MAX_PREVIOUS_MESSAGES];

    void addPreviousMessage(uint16_t address, String message);

    void orderPreviousMessagesByTime();

    String previousMessageToString(PreviousMessage* previousMessage);
};