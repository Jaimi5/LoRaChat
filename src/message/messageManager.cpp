#include "messageManager.h"

void MessageManager::init() {
    // xProcessQueue = xQueueCreate(10, sizeof(ManagerMessage*));
    // xSendQueue = xQueueCreate(10, sizeof(ManagerMessage*));

    // createTasks();
}

void MessageManager::addMessageService(MessageService* service) {
    //Add ordered by serviceId
    bool added = false;
    for (int i = 0; i < services.size(); i++) {
        if (services[i]->serviceId > service->serviceId) {
            services.insert(services.begin() + i, service);
            added = true;
            break;
        }
    }
    if (!added) {
        services.push_back(service);
    }
}

// void MessageManager::loopSendMessages(void*) {
//     MessageManager& manager = MessageManager::getInstance();
//     ManagerMessage* message;

//     for (;;) {
//         ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

//         if (manager.xProcessQueue != 0) {
//             // Receive a message on the created queue.  Block for 10 ticks if a
//             // message is not immediately available.
//             if (xQueueReceive(manager.xProcessQueue, &(message), (portTickType) 10)) {
//                 // pcRxedMessage now points to the struct AMessage variable posted
//                 // by vATask.
//                 manager.sendMessage(message);
//                 delete message->message;
//                 delete message;
//             }
//         }

//     }
// }

String MessageManager::getAvailableCommands() {
    String commands = "";

    for (auto service : services) {
        commands += service->toString() + CR;
        commands += service->commandService->publicCommands();
    }

    return commands;
}

String MessageManager::executeCommand(uint8_t serviceId, uint8_t commandId, String args) {
    for (auto service : services) {
        if (service->serviceId == serviceId) {
            return service->commandService->executeCommand(commandId, args);
        }
    }

    return "Service not found";
}

String MessageManager::executeCommand(uint8_t serviceId, String command) {
    String result = "";

    for (auto service : services) {
        if (service->serviceId == serviceId) {
            result += service->commandService->executeCommand(command);
        }
    }

    return result;
}

String MessageManager::executeCommand(String command) {
    String result = "";
    bool found = false;

    for (auto service : services) {
        if (service->commandService->hasCommand(command)) {
            found = true;
            result += service->commandService->executeCommand(command);
        }
    }

    if (!found) {
        result = "Command not found";
    }

    return result;
}

// void MessageManager::loopReceivedMessages(void*) {
//     MessageManager& manager = MessageManager::getInstance();
//     ManagerMessage* message;

//     for (;;) {

//         ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

//         if (manager.xProcessQueue != 0) {
//             // Receive a message on the created queue.  Block for 10 ticks if a
//             // message is not immediately available.
//             if (xQueueReceive(manager.xProcessQueue, &(message), (portTickType) 10)) {
//                 // pcRxedMessage now points to the struct AMessage variable posted
//                 // by vATask.
//                 manager.processReceivedMessage(message);
//                 delete message->message;
//                 delete message;
//             }
//         }
//     }
// }

// /**
//  * @brief Create a Bluetooth Task
//  *
//  */
// void MessageManager::createTasks() {
//     int res = xTaskCreate(
//         loopReceivedMessages,
//         "Manager Receive Task",
//         4096,
//         (void*) 1,
//         2,
//         &sendMessageManager_TaskHandle);
//     if (res != pdPASS) {
//         Log.errorln(F("Manager Receive task handle error: %d"), res);
//     }

//     res = xTaskCreate(
//         loopSendMessages,
//         "Manager Send Task",
//         4096,
//         (void*) 1,
//         2,
//         &receiveMessageManager_TaskHandle);
//     if (res != pdPASS) {
//         Log.errorln(F("Manager Send task handle error: %d"), res);
//     }
// }

void MessageManager::processReceivedMessage(messagePort port, DataMessage* message) {
    for (auto service : services) {
        if (service->serviceId == message->appPortDst) {
            service->processReceivedMessage(port, message);
        }
    }
}

void MessageManager::sendMessage(messagePort port, DataMessage* message) {
    switch (port) {
        case LoRaMeshPort:
            sendMessageLoRaMesher(message);
            break;
        case BluetoothPort:
            sendMessageBluetooth(message);
            break;
        case WiFiPort:
            sendMessageWiFi(message);
            break;
        default:
            break;
    }
}

// void MessageManager::sendCommand(messagePort port, uint8_t id, uint16_t dst, uint8_t appPortSrc, uint8_t appPortDst, uint8_t command, uint32_t size, uint8_t* args[]) {
//     DataMessage* message = new DataMessage();
//     message->appPortSrc = appPortSrc;
//     message->appPortDst = appPortDst;
//     message->command = command;
//     message->size = size;
//     message->args = args;
//     message->dst = dst;
//     message->src = serviceSrc;

//     sendMessage(port, message);
// }

void MessageManager::sendMessageLoRaMesher(DataMessage* message) {
    LoRaMeshService& mesher = LoRaMeshService::getInstance();
    mesher.sendReliable(message);
}