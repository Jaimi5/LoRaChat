#pragma once

#include <Arduino.h>

#include <LoraMesher.h>

#include "./config.h"

#include "contact.h"

#include "contactMessage.h"

#include "contactCommandService.h"

#include "./message/messageService.h"

#include "./gps/gpsService.h"

#include "./message/messageManager.h"

#include "contactCommandService.h"

//TODO: Add contact service (or bluetooth), only ask for contact info to thus that have bluetooth port open

class ContactService: public MessageService {
public:
    /**
     * @brief Construct a new ContactService object
     *
     */
    static ContactService& getInstance() {
        static ContactService instance;
        return instance;
    }

    void initContactService();

    void addContact(DataMessage* message);

    void addContact(String name, uint16_t src);

    String getNameContact(uint16_t addr);

    uint16_t getAddrContact(String name);

    String getContactsString();

    void changeName(String newName);

    String requestGPS(messagePort port, String dst);

    String responseGPS(messagePort port, DataMessage* message);

    String findContacts();

    ContactMessage* createContactMessage();

    void requestContactInfo(messagePort port, uint16_t dst);

    void responseContactInfo(messagePort port, ContactMessage* message);

    virtual void processReceivedMessage(messagePort port, DataMessage* message);

    ContactCommandService* contactCommandService = new ContactCommandService();

private:

    uint8_t requestId;

    ContactService(): MessageService(appPort::LoRaChat, String("LoRaChat")) {
        commandService = contactCommandService;
    };

    char myName[MAX_NAME_LENGTH] = "test";

    LM_LinkedList<Info>* contactsList = new LM_LinkedList<Info>();
};