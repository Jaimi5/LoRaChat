// #include "loraMeshService.h"

// void LoRaMeshService::initLoraMesherService() {
//     //Initialize LoRaMesher
//     radio.begin();

//     //Create the receive task and add it to the LoRaMesher
//     createReceiveMessages();

//     //Start LoRaMesher
//     radio.start();

//     Log.verboseln("LoraMesher initialized");
// }

// void LoRaMeshService::loopReceivedPackets() {
//     //Iterate through all the packets inside the Received User Packets FiFo
//     while (radio.getReceivedQueueSize() > 0) {
//         Log.traceln(F("LoRaPacket received"));
//         Log.traceln(F("Queue receiveUserData size: %d"), radio.getReceivedQueueSize());

//         //Get the first element inside the Received User Packets FiFo
//         AppPacket<DataMessage>* packet = radio.getNextAppPacket<DataMessage>();

//         //Process the packet
//         // MessageService::getInstance().processReceivedMessage(packet->src, packet->dst, packet->getPayloadLength(), packet->payload);

//         //Delete the packet when used. It is very important to call this function to release the memory of the packet.
//         radio.deletePacket(packet);

//     }
// }

// /**
//  * @brief Function that process the received packets
//  *
//  */
// void processReceivedPackets(void*) {
//     for (;;) {
//         /* Wait for the notification of processReceivedPackets and enter blocking */
//         ulTaskNotifyTake(pdPASS, portMAX_DELAY);
//         LoRaMeshService::getInstance().loopReceivedPackets();
//     }
// }


// /**
//  * @brief Create a Receive Messages Task and add it to the LoRaMesher
//  *
//  */
// void LoRaMeshService::createReceiveMessages() {
//     int res = xTaskCreate(
//         processReceivedPackets,
//         "Receive App Task",
//         4096,
//         (void*) 1,
//         2,
//         &receiveLoRaMessage_Handle);
//     if (res != pdPASS) {
//         Log.errorln(F("Receive App Task creation gave error: %d"), res);
//     }

//     radio.setReceiveAppDataTaskHandle(receiveLoRaMessage_Handle);
// }

// uint16_t LoRaMeshService::getDeviceID() {
//     return radio.getLocalAddress();
// }

// String LoRaMeshService::getRoutingTable() {
//     String routingTable = "";

//     //Set the routing table list that is being used and cannot be accessed (Remember to release use after usage)
//     LM_LinkedList<RouteNode>* routingTableList = radio.routingTableList();

//     routingTableList->setInUse();

//     for (int i = 0; i < radio.routingTableSize(); i++) {
//         RouteNode* rNode = (*routingTableList)[i];
//         NetworkNode node = rNode->networkNode;
//         routingTable = String(node.address) + "( " + String(node.metric) + ") - Via: " + String(rNode->via) + "\n";
//     }

//     //Release routing table list usage.
//     routingTableList->releaseInUse();

//     return routingTable;
// }