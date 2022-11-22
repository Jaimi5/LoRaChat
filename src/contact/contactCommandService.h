#pragma once

#include "Arduino.h"

#include "./commands/commandService.h"

#include "contactService.h"

class ContactCommandService: public CommandService {
public:

    ContactCommandService();

private:
    String getLocationCommand();
    static String searchContactsCommand();
    static String printContactsCommand();
    static String changeNameCommand();
    static String chatCommand();
    static String printRTCommand();
};