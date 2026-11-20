#include "messageManager.h"

#include "esp_ota_ops.h"

static const char* MANAGER_TAG = "MANAGER";

void MessageManager::init() {}

void MessageManager::addMessageService(MessageService* service) {
    // A command name belongs to the first service that registers it.
    std::vector<String> refused;
    CommandCatalogue catalogue = buildCatalogue();
    for (const Command& command : service->commandService->commands()) {
        if (catalogue.add(command.spec())) continue;
        ESP_LOGE(MANAGER_TAG,
                 "Command %s of service %s refused: name or id taken, bad error codes or a typed "
                 "declaration that is not valid",
                 command.getCommand().c_str(), service->serviceName.c_str());
        refused.push_back(command.getCommand());
    }
    for (const String& name : refused) service->commandService->removeCommand(name);

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

CommandReply MessageManager::executeCommand(const String& line, Origin origin, bool signedValid) {
    String text = line;
    text.trim();
    int space = text.indexOf(' ');
    String name = space < 0 ? text : text.substring(0, space);
    String args = space < 0 ? "" : text.substring(space + 1);
    args.trim();

    const Command* command = findCommand(name);
    if (command == nullptr) return CommandStatus::UNKNOWN;
    if (!commandPermitted(command->getPerm(), origin, signedValid)) return CommandStatus::PERM;
    return command->execute(args);
}

const Command* MessageManager::findCommand(const String& name) const {
    for (auto service : services) {
        const Command* command = service->commandService->find(name);
        if (command != nullptr) return command;
    }
    return nullptr;
}

const Command* MessageManager::findCommandById(uint8_t id) const {
    for (auto service : services) {
        const Command* command = service->commandService->findById(id);
        if (command != nullptr) return command;
    }
    return nullptr;
}

CommandCatalogue MessageManager::buildCatalogue() const {
    CommandCatalogue catalogue;
    for (auto service : services) {
        for (const Command& command : service->commandService->commands()) {
            catalogue.add(command.spec());
        }
    }
    return catalogue;
}

String MessageManager::help(Origin origin) const {
    String text;
    for (auto service : services) text += service->commandService->help(origin);
    return text;
}

CommandReply MessageManager::catalogue(const String& args) const {
    std::vector<std::string> pages = buildCatalogue().pages(esp_ota_get_app_description()->version);
    size_t page = 0;
    if (!parseCataloguePage(args.c_str(), pages.size(), page)) return CommandStatus::ARG;
    return String(pages[page - 1].c_str());
}

uint32_t MessageManager::commandAllowanceMs(const String& name) const {
    const Command* command = findCommand(name);
    return command != nullptr ? command->getAllowanceMs() : 0;
}

String MessageManager::describeReply(const String& name, const CommandReply& reply) const {
    if (reply.ok()) return reply.text;
    const Command* command = findCommand(name);
    if (reply.status == static_cast<uint8_t>(CommandStatus::ARG) && command != nullptr) {
        String usage = "Usage: " + command->getCommand();
        if (command->getUsage().length() > 0) usage += " " + command->getUsage();
        return usage;
    }
    std::string text = describeCommandStatus(reply.status, reply.text.c_str());
    if (!text.empty()) return String(text.c_str());
    const char* specific = command != nullptr ? command->codeText(reply.status) : nullptr;
    text = specific != nullptr ? specific
                               : "Error " + std::to_string(static_cast<unsigned>(reply.status));
    if (reply.text.length() > 0) text += std::string(" (") + reply.text.c_str() + ")";
    return String(text.c_str());
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

DataMessagePtr MessageManager::getDataMessage(const String& json) {
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
