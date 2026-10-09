#include "messageManager.h"

static const char* MANAGER_TAG = "MANAGER";

void MessageManager::init() {}

void MessageManager::addMessageService(MessageService* service) {
    for (const Command& command : service->commandService->commands()) {
        for (auto other : services) {
            if (other->commandService->find(command.getCommand()) != nullptr) {
                ESP_LOGE(MANAGER_TAG, "Command %s exists in services %s and %s",
                         command.getCommand().c_str(), other->serviceName.c_str(),
                         service->serviceName.c_str());
            }
        }
    }

    // Add ordered by serviceId
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

String MessageManager::executeCommand(const String& line, Origin origin, bool signedValid) {
    String text = line;
    text.trim();
    int space = text.indexOf(' ');
    String name = space < 0 ? text : text.substring(0, space);
    String args = space < 0 ? "" : text.substring(space + 1);
    args.trim();
    bool local = origin == Origin::SERIAL_CONSOLE;

    if (name.equalsIgnoreCase("/help")) return help(origin);
    for (auto service : services) {
        Command* command = service->commandService->find(name);
        if (command == nullptr) continue;
        if (!commandPermitted(command->getPerm(), origin, signedValid)) {
            return local ? "Not allowed" : "ERR perm";
        }
        return command->execute(args);
    }
    return local ? "Unknown command; /help lists them" : "ERR unknown";
}

String MessageManager::help(Origin origin) const {
    String text = "/help - List the commands\n";
    for (auto service : services) text += service->commandService->help(origin);
    return text;
}

String MessageManager::getJSON(DataMessage* message) {
    logHeader("JSON", message);

    for (auto service : services) {
        if (service->serviceId == message->appPortSrc) {
            return service->getJSON(message);
        }
    }

    ESP_LOGE(MANAGER_TAG, "Service Not Found");

    return "{\"Empty\":\"true\"}";
}

DataMessage* MessageManager::getDataMessage(String json) {
    DynamicJsonDocument doc(2048);

    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        ESP_LOGE(MANAGER_TAG, "deserializeJson() failed: %s", error.c_str());
        return nullptr;
    }

    JsonObject data = doc["data"];

    uint8_t serviceId = data["appPortSrc"];

    for (auto service : services) {
        if (service->serviceId == serviceId) {
            return service->getDataMessage(data);
        }
    }

    ESP_LOGE(MANAGER_TAG, "Service Not Found");

    return nullptr;
}

void MessageManager::logHeader(const char* title, const DataMessage* message) {
    ESP_LOGI(MANAGER_TAG, "%s src=%04X dst=%04X app %u->%u id=%u size=%u", title,
             message->addrSrc, message->addrDst, static_cast<unsigned>(message->appPortSrc),
             static_cast<unsigned>(message->appPortDst), static_cast<unsigned>(message->messageId),
             static_cast<unsigned>(message->messageSize));
}

void MessageManager::processReceivedMessage(messagePort port, DataMessage* message) {
    logHeader("Received", message);

    // TODO: Add a list to track the messages already received to avoid loops and duplicates

    if (message->addrDst != 0 &&
        message->addrDst != LoRaMeshService::getInstance().getLocalAddress()) {
        ESP_LOGI(MANAGER_TAG, "Message not for me");
        if (port == MqttPort) {
            sendMessage(LoRaMeshPort, message);
        }
        return;
    }

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
        case MqttPort:
            sendMessageMqtt(message);
            break;
        case InternalPort:
            processReceivedMessage(InternalPort, message);
            break;
        default:
            break;
    }
}

void MessageManager::sendMessageLoRaMesher(DataMessage* message) {
    LoRaMeshService& mesher = LoRaMeshService::getInstance();
    mesher.send(message);
}

void MessageManager::sendMessageMqtt(DataMessage* message) {
    MqttService& mqtt = MqttService::getInstance();
    if (mqtt.isInitialized() && mqtt.writeToMqtt(message)) {
        ESP_LOGI(MANAGER_TAG, "Message sent to MQTT");
        return;
    }

    LoRaMeshService& mesher = LoRaMeshService::getInstance();
    mesher.sendClosestGateway(message);
}
