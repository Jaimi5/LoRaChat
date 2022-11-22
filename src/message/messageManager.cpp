#include "messageManager.h"

void MessageManager::init() {
    xProcessQueue = xQueueCreate(10, sizeof(ManagerMessage*));
    xSendQueue = xQueueCreate(10, sizeof(ManagerMessage*));

    createTasks();
}

void MessageManager::addMessageService(MessageService* service) {
    for (int i = 0; i < 10; i++) {
        if (services[i] == nullptr) {
            services[i] = service;
            break;
        }
    }
}

void MessageManager::loopSendMessages(void*) {
    MessageManager& manager = MessageManager::getInstance();
    ManagerMessage* message;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (manager.xProcessQueue != 0) {
            // Receive a message on the created queue.  Block for 10 ticks if a
            // message is not immediately available.
            if (xQueueReceive(manager.xProcessQueue, &(message), (portTickType) 10)) {
                // pcRxedMessage now points to the struct AMessage variable posted
                // by vATask.
                manager.sendMessage(message);
                delete message->message;
                delete message;
            }
        }

    }
}

String MessageManager::getAvailableCommands() {
    String commands = "";
    for (int i = 0; i < 10; i++) {
        if (services[i] != nullptr) {
            commands += "Id: " + String(services[i]->serviceId) + " - " + services[i]->serviceName + "\n";
            commands += services[i]->commandService->publicCommands();
        }
    }

    return commands;
}

String MessageManager::executeCommand(uint8_t serviceId, String command) {
    String result = "";
    for (int i = 0; i < 10; i++) {
        if (services[i] != nullptr && services[i]->serviceId == serviceId) {
            result += services[i]->commandService->executeCommand(command);
        }
    }

    return result;
}

String MessageManager::executeCommand(String command) {
    String result = "";
    for (int i = 0; i < 10; i++) {
        if (services[i] != nullptr && services[i]->commandService->hasCommand(command)) {
            result += services[i]->commandService->executeCommand(command);
        }
    }

    return result;
}

void MessageManager::loopReceivedMessages(void*) {
    MessageManager& manager = MessageManager::getInstance();
    ManagerMessage* message;

    for (;;) {

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (manager.xProcessQueue != 0) {
            // Receive a message on the created queue.  Block for 10 ticks if a
            // message is not immediately available.
            if (xQueueReceive(manager.xProcessQueue, &(message), (portTickType) 10)) {
                // pcRxedMessage now points to the struct AMessage variable posted
                // by vATask.
                manager.processReceivedMessage(message);
                delete message->message;
                delete message;
            }
        }
    }
}

/**
 * @brief Create a Bluetooth Task
 *
 */
void MessageManager::createTasks() {
    int res = xTaskCreate(
        loopReceivedMessages,
        "Manager Receive Task",
        4096,
        (void*) 1,
        2,
        &sendMessageManager_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Manager Receive task handle error: %d"), res);
    }

    res = xTaskCreate(
        loopSendMessages,
        "Manager Send Task",
        4096,
        (void*) 1,
        2,
        &receiveMessageManager_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Manager Send task handle error: %d"), res);
    }
}


void MessageManager::processReceivedMessage(ManagerMessage* message) {
    for (int i = 0; i < 10; i++) {
        if (services[i] != nullptr && services[i]->serviceId == message->port) {
            Log.verboseln(F("Service found"));
            services[i]->processReceivedMessage(message->message);
        }
    }

    delete message->message;
    delete message;
}

void MessageManager::sendMessage(ManagerMessage* message) {
    switch (message->port) {
        case LoRaMeshPort:
            sendMessageLoRaMesher(message->message);
            break;
        case BluetoothPort:
            sendMessageBluetooth(message->message);
            break;
        case WiFiPort:
            sendMessageWiFi(message->message);
            break;
        default:
            break;
    }

    delete message->message;
    delete message;
}