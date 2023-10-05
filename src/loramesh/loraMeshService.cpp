#include "loraMeshService.h"

static const char* LMS_TAG = "LoRaMeshService";

void LoRaMeshService::initLoraMesherService() {
    // Set the LoRaMesher config
    LoraMesher::LoraMesherConfig config = LoraMesher::LoraMesherConfig();
    config.module = LoraMesher::LoraModules::SX1276_MOD;

    //Initialize LoRaMesher
    radio.begin(config);

    //Create the receive task and add it to the LoRaMesher
    createReceiveMessages();

    //Start LoRaMesher
    radio.start();

    ESP_LOGV(LMS_TAG, "LoraMesher initialized");
}

void LoRaMeshService::loopReceivedPackets() {
    //Iterate through all the packets inside the Received User Packets FiFo
    while (radio.getReceivedQueueSize() > 0) {
        ESP_LOGV(LMS_TAG, "LoRaPacket received");
        ESP_LOGV(LMS_TAG, "Queue receiveUserData size: %d", radio.getReceivedQueueSize());
        ESP_LOGV(LMS_TAG, "Heap size receive: %d", ESP.getFreeHeap());

        //Get the first element inside the Received User Packets FiFo
        AppPacket<LoRaMeshMessage>* packet = radio.getNextAppPacket<LoRaMeshMessage>();

        //Create a DataMessage from the received packet
        DataMessage* message = createDataMessage(packet);

        //Process the packet
        MessageManager::getInstance().processReceivedMessage(LoRaMeshPort, message);

        //Delete the message
        vPortFree(message);

        //Delete the packet when used. It is very important to call this function to release the memory of the packet.
        radio.deletePacket(packet);
        ESP_LOGV(LMS_TAG, "Heap size receive2: %d", ESP.getFreeHeap());
    }
}

/**
 * @brief Function that process the received packets
 *
 */
void processReceivedPackets(void*) {
    for (;;) {
        /* Wait for the notification of processReceivedPackets and enter blocking */
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);
        LoRaMeshService::getInstance().loopReceivedPackets();
    }
}


/**
 * @brief Create a Receive Messages Task and add it to the LoRaMesher
 *
 */
void LoRaMeshService::createReceiveMessages() {
    int res = xTaskCreate(
        processReceivedPackets,
        "Receive App Task",
        4096,
        (void*) 1,
        2,
        &receiveLoRaMessage_Handle);
    if (res != pdPASS) {
        ESP_LOGE(LMS_TAG, "Receive App Task creation gave error: %d", res);
    }

    radio.setReceiveAppDataTaskHandle(receiveLoRaMessage_Handle);
}

LoRaMeshMessage* LoRaMeshService::createLoRaMeshMessage(DataMessage* message) {
    LoRaMeshMessage* loraMeshMessage = (LoRaMeshMessage*) pvPortMalloc(sizeof(LoRaMeshMessage) + message->messageSize);

    if (loraMeshMessage) {
        loraMeshMessage->appPortDst = message->appPortDst;
        loraMeshMessage->appPortSrc = message->appPortSrc;
        loraMeshMessage->messageId = message->messageId;
        memcpy(loraMeshMessage->dataMessage, message->message, message->messageSize);
    }

    return loraMeshMessage;
}

DataMessage* LoRaMeshService::createDataMessage(AppPacket<LoRaMeshMessage>* appPacket) {
    uint32_t dataMessageSize = appPacket->payloadSize + sizeof(DataMessage) - sizeof(LoRaMeshMessage);
    uint32_t messageSize = dataMessageSize - sizeof(DataMessage);

    DataMessage* dataMessage = (DataMessage*) pvPortMalloc(dataMessageSize);

    if (dataMessage) {
        LoRaMeshMessage* message = appPacket->payload;

        dataMessage->appPortDst = message->appPortDst;
        dataMessage->appPortSrc = message->appPortSrc;
        dataMessage->messageId = message->messageId;

        dataMessage->addrSrc = appPacket->src;
        dataMessage->addrDst = appPacket->dst;

        dataMessage->messageSize = messageSize;

        memcpy(dataMessage->message, message->dataMessage, messageSize);
    }

    return dataMessage;
}

uint16_t LoRaMeshService::getLocalAddress() {
    return radio.getLocalAddress();
}

String LoRaMeshService::getRoutingTable() {
    String routingTable = "--- Routing Table ---\n";

    //Set the routing table list that is being used and cannot be accessed (Remember to release use after usage)
    LM_LinkedList<RouteNode>* routingTableList = radio.routingTableListCopy();

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* routeNode = routingTableList->getCurrent();
            NetworkNode node = routeNode->networkNode;
            routingTable += String(node.address) + " (" + String(node.metric) + ") - Via: " + String(routeNode->via) + "\n";
        } while (routingTableList->next());
    }
    else {
        routingTable += "No routes";
    }

    //Release routing table list usage.
    routingTableList->releaseInUse();

    routingTableList->Clear();

    return routingTable;
}

void LoRaMeshService::sendReliable(DataMessage* message) {
    ESP_LOGV(LMS_TAG, "Heap size send: %d", ESP.getFreeHeap());

    LoRaMeshMessage* loraMeshMessage = createLoRaMeshMessage(message);

    radio.sendReliablePacket(message->addrDst, (uint8_t*) loraMeshMessage, sizeof(LoRaMeshMessage) + message->messageSize);

    vPortFree(loraMeshMessage);
    ESP_LOGV(LMS_TAG, "Heap size send 2: %d", ESP.getFreeHeap());
}

bool LoRaMeshService::sendClosestGateway(DataMessage* message) {
    RouteNode* gatewayNode = radio.getClosestGateway();

    if (!gatewayNode) {
        ESP_LOGE(LMS_TAG, "No gateway found");
        return false;
    }

    message->addrDst = gatewayNode->networkNode.address;

    ESP_LOGI(LMS_TAG, "Sending message to gateway %X", message->addrDst);

    sendReliable(message);

    return true;
}

bool LoRaMeshService::hasActiveConnections() {
    return radio.hasActiveConnections();
}

bool LoRaMeshService::hasActiveSentConnections() {
    return radio.hasActiveSentConnections();
}

bool LoRaMeshService::hasActiveReceivedConnections() {
    return radio.hasActiveReceivedConnections();
}

void LoRaMeshService::standby() {
    return radio.standby();
}
