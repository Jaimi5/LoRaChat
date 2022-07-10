#include "contacts.h"

void Contact::addContact(String name, uint16_t src) {
    contactsList->setInUse();
    if (contactsList->moveToStart()) {
        do {
            Info* ci = contactsList->getCurrent();
            if (ci) {
                if (ci->address == src) {
                    name.toCharArray(ci->name, MAX_NAME_LENGTH);
                    contactsList->releaseInUse();
                    return;
                }
            }

        } while (contactsList->next());
    }

    Info* ci = new Info();
    ci->address = src;
    name.toCharArray(ci->name, MAX_NAME_LENGTH);

    contactsList->Append(ci);
    contactsList->releaseInUse();
}

String Contact::getNameContact(uint16_t addr) {
    contactsList->setInUse();
    if (contactsList->moveToStart()) {
        do {
            Info* ci = contactsList->getCurrent();
            if (ci) {
                if (ci->address == addr) {
                    contactsList->releaseInUse();
                    return String(ci->name);
                }
            }

        } while (contactsList->next());
    }

    contactsList->releaseInUse();
    return "";
}

uint16_t Contact::getAddrContact(String name) {
    contactsList->setInUse();
    if (contactsList->moveToStart()) {
        do {
            Info* ci = contactsList->getCurrent();
            if (ci) {
                if (String(ci->name) == name) {
                    contactsList->releaseInUse();
                    return ci->address;
                }
            }

        } while (contactsList->next());
    }

    contactsList->releaseInUse();
    return 0;
}

String Contact::getContactsString() {
    String contacts = "--- List of contacts ---";
    contactsList->setInUse();
    if (contactsList->moveToStart()) {
        do {
            Info* contact = contactsList->getCurrent();
            if (contact) {
                contacts += F(CR " - ");
                contacts += (String) contact->address + ": " + contact->name;
            }
        } while (contactsList->next());
    }
    else {
        contacts += F(CR "Empty List");
    }
    contactsList->releaseInUse();

    contacts += F(CR);
    return contacts;
}

void Contact::changeName(String newName) {
    newName.toCharArray(myName, MAX_NAME_LENGTH);
}

Contact contactService = *(new Contact());