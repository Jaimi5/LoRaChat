#include "loraMeshService.h"

void LoRaMeshService::initLoraMesherService() {
    //Initialize LoRaMesher
    radio.begin();

    //Create the receive task and add it to the LoRaMesher
    createReceiveMessages();

    //Start LoRaMesher
    radio.start();

    Log.verboseln("LoraMesher initialized");
}

void LoRaMeshService::loopReceivedPackets() {
    //Iterate through all the packets inside the Received User Packets FiFo
    while (radio.getReceivedQueueSize() > 0) {
        Log.traceln(F("LoRaPacket received"));
        Log.traceln(F("Queue receiveUserData size: %d"), radio.getReceivedQueueSize());

        //Get the first element inside the Received User Packets FiFo
        AppPacket<LoRaMeshMessage>* packet = radio.getNextAppPacket<LoRaMeshMessage>();

        //Create a DataMessage from the received packet
        DataMessage* message = createDataMessage(packet);

        //Process the packet
        MessageManager::getInstance().processReceivedMessage(LoRaMeshPort, message);

        //Delete the message
        free(message);

        //Delete the packet when used. It is very important to call this function to release the memory of the packet.
        radio.deletePacket(packet);
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
        Log.errorln(F("Receive App Task creation gave error: %d"), res);
    }

    radio.setReceiveAppDataTaskHandle(receiveLoRaMessage_Handle);
}

LoRaMeshMessage* LoRaMeshService::createLoRaMeshMessage(DataMessage* message) {
    LoRaMeshMessage* loraMeshMessage = (LoRaMeshMessage*) malloc(sizeof(LoRaMeshMessage) + message->messageSize);

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

    DataMessage* dataMessage = (DataMessage*) malloc(dataMessageSize);

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

uint16_t LoRaMeshService::getDeviceID() {
    return radio.getLocalAddress();
}

String LoRaMeshService::getRoutingTable() {
    String routingTable = "--- Routing Table ---\n";

    //Set the routing table list that is being used and cannot be accessed (Remember to release use after usage)
    LM_LinkedList<RouteNode>* routingTableList = radio.routingTableList();

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

    return routingTable;
}

void LoRaMeshService::sendReliable(DataMessage* message) {
    LoRaMeshMessage* loraMeshMessage = createLoRaMeshMessage(message);

    radio.sendReliablePacket(message->addrDst, (uint8_t*) loraMeshMessage, sizeof(LoRaMeshMessage) + message->messageSize);

    free(loraMeshMessage);
}
