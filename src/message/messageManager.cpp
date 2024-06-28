#include "messageManager.h"

static const char* MANAGER_TAG = "MANAGER";

void MessageManager::init() {
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

String MessageManager::getAvailableCommands() {
    String commands = "";

    for (auto service : services) {
        commands += service->toString() + "\n";
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

String MessageManager::getJSON(DataMessage* message) {
    printDataMessageHeader("JSON", message);

    for (auto service : services) {
        if (service->serviceId == message->appPortSrc) {
            return service->getJSON(message);
        }
    }

    ESP_LOGE(MANAGER_TAG, "Service Not Found");

    return "{\"Empty\":\"true\"}";
}

DataMessage* MessageManager::getDataMessage(String json) {
    DynamicJsonDocument doc(1024);

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

String MessageManager::printDataMessageHeader(String title, DataMessage* message) {
    DynamicJsonDocument doc(1024);

    doc["title"] = title;

    JsonObject data = doc.createNestedObject("data");

    message->serialize(data);

    String json;
    serializeJson(doc, json);

    ESP_LOGI(MANAGER_TAG, "%s", json.c_str());

    return json;
}

void MessageManager::processReceivedMessage(messagePort port, DataMessage* message) {
    printDataMessageHeader("Received", message);

    // TODO: Add a list to track the messages already received to avoid loops and duplicates

    if (message->addrDst != 0 && message->addrDst != LoRaMeshService::getInstance().getLocalAddress()) {
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
        case BluetoothPort:
            sendMessageBluetooth(message);
            break;
        case WiFiPort:
            sendMessageWiFi(message);
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

void MessageManager::sendMessageWiFi(DataMessage* message) {
    WiFiServerService& wifi = WiFiServerService::getInstance();
    // if (wifi.processReceivedMessage(message) {
    //     ESP_LOGI(MANAGER_TAG,"Message sent to WiFi");
    //     return;
    // }

    if (wifi.isConnected()) {
        ESP_LOGE(MANAGER_TAG, "Error sending message to WiFi");
        //TODO: Retry adding it into a queue and send it later or send to closest gateway 
        return;
    }
    else
        ESP_LOGE(MANAGER_TAG, "WiFi not connected");

    LoRaMeshService& mesher = LoRaMeshService::getInstance();
    mesher.sendClosestGateway(message);
}
