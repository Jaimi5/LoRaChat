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

String MessageManager::getAvailableCommandsHTML() {
    String html = "<dl>";

    for (auto service : services) {
        html += "<dt>" + service->toString() + "</dt>";
        html += service->commandService->publicCommandsHTML();
    }

    html += "</dl>";

    return html;
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

String MessageManager::getJSON(DataMessage* message) {
    printDataMessageHeader("JSON", message);

    for (auto service : services) {
        if (service->serviceId == message->appPortSrc) {
            return service->getJSON(message);
        }
    }

    Log.errorln("Service Not Found");

    return "{\"Empty\":\"true\"}";
}

String MessageManager::printDataMessageHeader(String title, DataMessage* message) {
    DynamicJsonDocument doc(1024);

    doc["title"] = title;

    JsonObject data = doc.createNestedObject("dataMessage");

    message->serialize(data);

    String json;
    serializeJson(doc, json);

    Log.verboseln(json.c_str());

    return json;
}

void MessageManager::processReceivedMessage(messagePort port, DataMessage* message) {
    printDataMessageHeader("Received", message);

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
        case MqttPort:
            sendMessageMqtt(message);
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

void MessageManager::sendMessageMqtt(DataMessage* message) {
    MqttService& mqtt = MqttService::getInstance();
    mqtt.writeToMqtt(message);
}

void MessageManager::sendMessageWiFi(DataMessage* message) {
    WiFiServerService& wifi = WiFiServerService::getInstance();
    if (wifi.connectAndSend(message)) {
        Log.verboseln(F("Message sent to WiFi"));
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Log.errorln(F("Error sending message to WiFi"));
        //TODO: Retry adding it into a queue and send it later or send to closest gateway 
        return;
    }
    else
        Log.errorln(F("WiFi not connected"));

    LoRaMeshService& mesher = LoRaMeshService::getInstance();
    mesher.sendClosestGateway(message);
}
