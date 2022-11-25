#pragma once

#include "Arduino.h"

#include "./commands/commandService.h"

#include "contactMessage.h"

class ContactCommandService: public CommandService {
public:

    ContactCommandService();
};