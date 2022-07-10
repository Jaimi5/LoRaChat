#ifndef Contacts
#define Contacts

#include <Arduino.h>

#include <LoraMesher.h>

#define MAX_NAME_LENGTH 10

class Contact {
public:
    char myName[MAX_NAME_LENGTH] = "test";

#pragma pack(1)
    struct Info {
        uint16_t address;
        char name[MAX_NAME_LENGTH];
    };
#pragma pack()

    LM_LinkedList<Info>* contactsList = new LM_LinkedList<Info>();
    void addContact(String name, uint16_t src);

    String getNameContact(uint16_t addr);

    uint16_t getAddrContact(String addr);

    String getContactsString();

    void changeName(String newName);

};

extern Contact contactService;

#endif