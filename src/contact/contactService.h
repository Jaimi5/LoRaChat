#pragma once

#include <Arduino.h>

#include <LoraMesher.h>

#include "contact.h"

//TODO: Add contact service (or bluetooth), only ask for contact info to thus that have bluetooth port open

class ContactService {
public:
    /**
     * @brief Construct a new ContactService object
     *
     */
    static ContactService& getInstance() {
        static ContactService instance;
        return instance;
    }

    void addContact(String name, uint16_t src);

    String getNameContact(uint16_t addr);

    uint16_t getAddrContact(String addr);

    String getContactsString();

    void changeName(String newName);

private:

    ContactService() {};

    char myName[MAX_NAME_LENGTH] = "test";

    LM_LinkedList<Info>* contactsList = new LM_LinkedList<Info>();
};