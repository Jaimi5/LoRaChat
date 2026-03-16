#include "loraMeshService.h"
#include "esp_heap_caps.h"

static const char* LMS_TAG = "LoRaMeshService";

// #define LORA_CS 18    // SPI Chip Select (NSS)
// #define LORA_RST 23   // Radio Reset pin
// #define LORA_IRQ 26   // DIO0 - Primary interrupt
// #define LORA_IO1 33   // DIO1 - Secondary interrupt


#ifdef USE_LORAMESHER_V2
// ============================================================
// LoRaMesher v2 (Builder pattern, callback-based)
// ============================================================

void LoRaMeshService::initLoraMesherService() {
#ifdef LORA_ENABLED
    loramesher::PinConfig pinConfig(LORA_CS, LORA_RST, LORA_IRQ, LORA_IO1, LORA_SCK, LORA_MISO,
                                    LORA_MOSI);

    // Step 2: Configure radio parameters
    loramesher::RadioConfig radioConfig(LORA_RADIO_TYPE, LORA_FREQUENCY, LORA_SPREADING_FACTOR,
                                        LORA_BANDWIDTH, LORA_CODING_RATE, LORA_POWER,
                                        LORA_SYNC_WORD, LORA_CRC, LORA_PREAMBLE_LENGTH);

    // #ifdef LORA_MODULE_SX1276
    //     loramesher::RadioConfig radioConfig(loramesher::RadioType::kSx1276);
    // #elif defined(LORA_MODULE_SX1278)
    //     loramesher::RadioConfig radioConfig(loramesher::RadioType::kSx1278);
    // #else
    //     loramesher::RadioConfig radioConfig(loramesher::RadioType::kSx1276);
    // #endif

    loramesher::LoRaMeshProtocolConfig meshConfig;

    loramesher::AddressType my_address = loramesher::LoraMesher::GenerateAddressFromHardware();

#if defined(LORA_MANAGER_ID)
    // Optional: Set role based on address
    if (my_address == LORA_MANAGER_ID) {
        meshConfig.setNodeRole(loramesher::NodeRole::NETWORK_MANAGER);
    } else {
        meshConfig.setNodeRole(loramesher::NodeRole::NODE_ONLY);
    }
#endif

    meshConfig.setTargetDutyCycle(LORA_DUTY_CYCLE);
    meshConfig.setMaxPacketSize(LORA_MAX_PACKET_SIZE);
    meshConfig.setMinSleepFraction(LORA_MIN_SLEEP_FRACTION);

    mesher_ = loramesher::LoraMesher::Builder()
                  .withRadioConfig(radioConfig)
                  .withPinConfig(pinConfig)
                  .withLoRaMeshProtocol(meshConfig)
                  .Build();

    mesher_->SetDataCallback([](loramesher::AddressType source, const std::vector<uint8_t>& data) {
        ESP_LOGV(LMS_TAG, "v2 data callback from %04X, size=%d", source, data.size());

        if (data.size() < sizeof(LoRaMeshMessage)) {
            ESP_LOGW(LMS_TAG, "Received packet too small");
            return;
        }

        const LoRaMeshMessage* lmMsg = reinterpret_cast<const LoRaMeshMessage*>(data.data());
        uint32_t messagePayloadSize = data.size() - sizeof(LoRaMeshMessage);
        uint32_t dataMessageSize = sizeof(DataMessage) + messagePayloadSize;

        DataMessage* dataMessage = (DataMessage*)pvPortMalloc(dataMessageSize);
        if (dataMessage) {
            dataMessage->appPortDst = lmMsg->appPortDst;
            dataMessage->appPortSrc = lmMsg->appPortSrc;
            dataMessage->messageId = lmMsg->messageId;
            dataMessage->addrSrc = source;
            dataMessage->addrDst = LoRaMeshService::getInstance().getLocalAddress();
            dataMessage->messageSize = messagePayloadSize;
            memcpy(dataMessage->message, lmMsg->dataMessage, messagePayloadSize);

            MessageManager::getInstance().processReceivedMessage(LoRaMeshPort, dataMessage);

            vPortFree(dataMessage);
        }
    });

    heap_caps_check_integrity_all(true);
    auto result = mesher_->Start();
    heap_caps_check_integrity_all(true);
    if (result) {
        ESP_LOGI(LMS_TAG, "LoraMesher v2 initialized");
    } else {
        ESP_LOGE(LMS_TAG, "LoraMesher v2 Start failed");
    }
#endif
}

uint16_t LoRaMeshService::getLocalAddress() {
    return mesher_ ? mesher_->GetNodeAddress()
                   : loramesher::LoraMesher::GenerateAddressFromHardware();
}

String LoRaMeshService::getRoutingTable() {
    String routingTable = "--- Routing Table ---\n";

    if (!mesher_) {
        routingTable += "No mesher";
        return routingTable;
    }

    auto routes = mesher_->GetRoutingTable();
    if (routes.empty()) {
        routingTable += "No routes";
    } else {
        for (const auto& route : routes) {
            routingTable += String(route.destination) + " (hops:" + String(route.hop_count) +
                            " lq:" + String(route.link_quality) +
                            ") - Via: " + String(route.next_hop) + "\n";
        }
    }

    return routingTable;
}

void LoRaMeshService::send(DataMessage* message) {
    if (!mesher_)
        return;

    ESP_LOGV(LMS_TAG, "Heap size send: %d", ESP.getFreeHeap());

    LoRaMeshMessage* loraMeshMessage = createLoRaMeshMessage(message);
    if (!loraMeshMessage)
        return;

    size_t totalSize = sizeof(LoRaMeshMessage) + message->messageSize;
    heap_caps_check_integrity_all(true);
    std::vector<uint8_t> payload(reinterpret_cast<uint8_t*>(loraMeshMessage),
                                 reinterpret_cast<uint8_t*>(loraMeshMessage) + totalSize);

    auto result = mesher_->Send(message->addrDst, payload);
    if (!result) {
        ESP_LOGW(LMS_TAG, "v2 Send failed");
    }

    vPortFree(loraMeshMessage);
    heap_caps_check_integrity_all(true);
    ESP_LOGV(LMS_TAG, "Heap size send 2: %d", ESP.getFreeHeap());
}

bool LoRaMeshService::sendClosestGateway(DataMessage* message) {
    if (!mesher_)
        return false;

    auto gateway = mesher_->GetClosestGateway();
    if (!gateway.has_value()) {
        ESP_LOGE(LMS_TAG, "No gateway found");
        return false;
    }

    message->addrDst = gateway->destination;
    ESP_LOGI(LMS_TAG, "Sending message to gateway %X", message->addrDst);
    send(message);
    return true;
}

void LoRaMeshService::setGateway() {
    if (mesher_) {
        mesher_->SetNodeCapabilities(loramesher::NodeCapabilities::GATEWAY);
    }
}

void LoRaMeshService::removeGateway() {
    if (mesher_) {
        mesher_->SetNodeCapabilities(loramesher::NodeCapabilities::NONE);
    }
}

bool LoRaMeshService::hasActiveConnections() {
    if (!mesher_)
        return false;

    // TODO: Implement this.
    //  return mesher_->GetPendingTXPackets();
    return false;
}

bool LoRaMeshService::hasActiveSentConnections() {
    // v2 doesn't distinguish sent/received connections
    return hasActiveConnections();
}

bool LoRaMeshService::hasActiveReceivedConnections() {
    // v2 doesn't distinguish sent/received connections
    return hasActiveConnections();
}

size_t LoRaMeshService::queueWaitingSendPacketsLength() {
    // v2 doesn't expose queue sizes directly
    return 0;
}

void LoRaMeshService::standby() {
    if (mesher_) {
        mesher_->Stop();
    }
}

bool LoRaMeshService::hasGateway() {
    if (!mesher_)
        return false;
    return mesher_->GetClosestGateway().has_value();
}

void LoRaMeshService::updateRoutingTable() {
    // v2 routing table is always fresh from GetRoutingTable()
    if (mesher_ && mesher_->GetRoutingTable().empty()) {
        ESP_LOGW(LMS_TAG, "No routes in the routing table");
    }
}

std::vector<loramesher::RouteEntry> LoRaMeshService::getRoutingTableEntries() {
    if (!mesher_)
        return {};
    return mesher_->GetRoutingTable();
}

LoRaMeshMessage* LoRaMeshService::createLoRaMeshMessage(DataMessage* message) {
    LoRaMeshMessage* loraMeshMessage =
        (LoRaMeshMessage*)pvPortMalloc(sizeof(LoRaMeshMessage) + message->messageSize);

    if (loraMeshMessage) {
        loraMeshMessage->appPortDst = message->appPortDst;
        loraMeshMessage->appPortSrc = message->appPortSrc;
        loraMeshMessage->messageId = message->messageId;
        memcpy(loraMeshMessage->dataMessage, message->message, message->messageSize);
    }

    return loraMeshMessage;
}


size_t LoRaMeshService::GetRxQueueSize() const {
    return mesher_->GetRxQueueSize();
}

size_t LoRaMeshService::GetTxQueueSize() const {
    return mesher_->GetTxQueueSize();
}

uint32_t LoRaMeshService::getTimeUntilNextDataSlot(uint32_t guard_time_ms) const {
    if (!mesher_)
        return 0;
    return mesher_->GetTimeUntilNextDataSlot(guard_time_ms);
}

#else
// ============================================================
// LoRaMesher v1 (singleton, task-notification API)
// ============================================================

#if defined(NAYAD_V1) || defined(NAYAD_V1R2) || defined(T_BEAM_LORA_32) || defined(T_BEAM_V10) || \
    defined(T_BEAM_V12)
SPIClass newSPI(HSPI);
#endif

void LoRaMeshService::initLoraMesherService() {
#ifdef LORA_ENABLED
    LoraMesher::LoraMesherConfig config = LoraMesher::LoraMesherConfig();

    config.loraCs = LORA_CS;
    config.loraRst = LORA_RST;
    config.loraIrq = LORA_IRQ;
    config.loraIo1 = LORA_IO1;

    ESP_LOGV(LMS_TAG, "LoraMesher config: CS: %d, RST: %d, IRQ: %d, IO1: %d", config.loraCs,
             config.loraRst, config.loraIrq, config.loraIo1);

#ifdef LORA_MODULE_SX1276
    config.module = LoraMesher::LoraModules::SX1276_MOD;
#elif defined(LORA_MODULE_SX1262)
    config.module = LoraMesher::LoraModules::SX1262_MOD;
#endif

#if defined(NAYAD_V1) || defined(NAYAD_V1R2) || defined(T_BEAM_LORA_32) || defined(T_BEAM_V10) || \
    defined(T_BEAM_V12)
    newSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    config.spi = &newSPI;
#endif

    ESP_LOGV(LMS_TAG, "LoraMesher config: Module: %d", config.module);
    ESP_LOGV(LMS_TAG, "LoraMesher config: LORA_SCK: %d, LORA_MISO: %d, LORA_MOSI: %d, LORA_CS: %d",
             LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

    // Initialize LoRaMesher
    radio.begin(config);

    // Create the receive task and add it to the LoRaMesher
    createReceiveMessages();

    // Start LoRaMesher
    radio.start();

    ESP_LOGV(LMS_TAG, "LoraMesher initialized");
#endif
}

void LoRaMeshService::loopReceivedPackets() {
    // Iterate through all the packets inside the Received User Packets FiFo
    while (radio.getReceivedQueueSize() > 0) {
        ESP_LOGV(LMS_TAG, "LoRaPacket received");
        ESP_LOGV(LMS_TAG, "Queue receiveUserData size: %d", radio.getReceivedQueueSize());
        ESP_LOGV(LMS_TAG, "Heap size receive: %d", ESP.getFreeHeap());

        // Get the first element inside the Received User Packets FiFo
        AppPacket<LoRaMeshMessage>* packet = radio.getNextAppPacket<LoRaMeshMessage>();

        // Create a DataMessage from the received packet
        DataMessage* message = createDataMessage(packet);

        // Process the packet
        MessageManager::getInstance().processReceivedMessage(LoRaMeshPort, message);

        // Delete the message
        vPortFree(message);

        // Delete the packet when used. It is very important to call this function to release the
        // memory of the packet.
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
        ESP_LOGV(LMS_TAG, "Stack space unused after entering the task: %d",
                 uxTaskGetStackHighWaterMark(NULL));

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
    int res = xTaskCreate(processReceivedPackets, "Receive App Task", 5000, (void*)1, 2,
                          &receiveLoRaMessage_Handle);
    if (res != pdPASS) {
        ESP_LOGE(LMS_TAG, "Receive App Task creation gave error: %d", res);
    }

    radio.setReceiveAppDataTaskHandle(receiveLoRaMessage_Handle);
}

LoRaMeshMessage* LoRaMeshService::createLoRaMeshMessage(DataMessage* message) {
    LoRaMeshMessage* loraMeshMessage =
        (LoRaMeshMessage*)pvPortMalloc(sizeof(LoRaMeshMessage) + message->messageSize);

    if (loraMeshMessage) {
        loraMeshMessage->appPortDst = message->appPortDst;
        loraMeshMessage->appPortSrc = message->appPortSrc;
        loraMeshMessage->messageId = message->messageId;
        memcpy(loraMeshMessage->dataMessage, message->message, message->messageSize);
    }

    return loraMeshMessage;
}

DataMessage* LoRaMeshService::createDataMessage(AppPacket<LoRaMeshMessage>* appPacket) {
    uint32_t dataMessageSize =
        appPacket->payloadSize + sizeof(DataMessage) - sizeof(LoRaMeshMessage);
    uint32_t messageSize = dataMessageSize - sizeof(DataMessage);

    DataMessage* dataMessage = (DataMessage*)pvPortMalloc(dataMessageSize);

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

    // Set the routing table list that is being used and cannot be accessed (Remember to release use
    // after usage)
    LM_LinkedList<RouteNode>* routingTableList = radio.routingTableListCopy();

    routingTableList->setInUse();

    if (routingTableList->moveToStart()) {
        do {
            RouteNode* routeNode = routingTableList->getCurrent();
            NetworkNode node = routeNode->networkNode;
            routingTable += String(node.address) + " (" + String(node.metric) +
                            ") - Via: " + String(routeNode->via) + "\n";
        } while (routingTableList->next());
    } else {
        routingTable += "No routes";
    }

    // Release routing table list usage.
    routingTableList->releaseInUse();

    routingTableList->Clear();

    return routingTable;
}

void LoRaMeshService::send(DataMessage* message) {
    ESP_LOGV(LMS_TAG, "Heap size send: %d", ESP.getFreeHeap());

    LoRaMeshMessage* loraMeshMessage = createLoRaMeshMessage(message);

#if SEND_RELIABLE == 0
    radio.createPacketAndSend(message->addrDst, (uint8_t*)loraMeshMessage,
                              sizeof(LoRaMeshMessage) + message->messageSize);
#else
    radio.sendReliablePacket(message->addrDst, (uint8_t*)loraMeshMessage,
                             sizeof(LoRaMeshMessage) + message->messageSize);
#endif

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

    send(message);

    return true;
}

void LoRaMeshService::setGateway() {
    LoraMesher::getInstance().addGatewayRole();
}

void LoRaMeshService::removeGateway() {
    LoraMesher::getInstance().removeGatewayRole();
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

size_t LoRaMeshService::queueWaitingSendPacketsLength() {
    return radio.queueWaitingSendPacketsLength();
}

void LoRaMeshService::standby() {
    return radio.standby();
}

bool LoRaMeshService::hasGateway() {
    RouteNode* gatewayNode = radio.getClosestGateway();

    return gatewayNode != nullptr;
}

void LoRaMeshService::updateRoutingTable() {
    if (radio.routingTableSize() == 0) {
        ESP_LOGW(LMS_TAG, "No routes in the routing table");
    }

    if (routingTableList != NULL) {
        routingTableList->Clear();
        delete (routingTableList);
    }

    routingTableList = radio.routingTableListCopy();
}

#endif  // USE_LORAMESHER_V2
