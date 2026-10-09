#include "loraMeshService.h"

static const char* LMS_TAG = "LoRaMeshService";


void LoRaMeshService::initLoraMesherService(bool networkManager) {
#ifdef LORA_ENABLED
    loramesher::PinConfig pinConfig(LORA_CS, LORA_RST, LORA_IRQ, LORA_IO1, LORA_SCK, LORA_MISO,
                                    LORA_MOSI);

    // Step 2: Configure radio parameters
    loramesher::RadioConfig radioConfig(LORA_RADIO_TYPE, LORA_FREQUENCY, LORA_SPREADING_FACTOR,
                                        LORA_BANDWIDTH, LORA_CODING_RATE, LORA_POWER,
                                        LORA_SYNC_WORD, LORA_CRC, LORA_PREAMBLE_LENGTH);

    loramesher::LoRaMeshProtocolConfig meshConfig;

    meshConfig.setNodeRole(networkManager ? loramesher::NodeRole::NETWORK_MANAGER
                                          : loramesher::NodeRole::NODE_ONLY);

    meshConfig.setTargetDutyCycle(LORA_DUTY_CYCLE);
    meshConfig.setMaxPacketSize(LORA_MAX_PACKET_SIZE);
    meshConfig.setMinSleepFraction(LORA_MIN_SLEEP_FRACTION);
    meshConfig.setDefaultDataSlots(LORA_DEFAULT_DATA_SLOTS);
    meshConfig.setMaxNetworkNodes(LORA_MAX_NETWORK_NODES);
    meshConfig.setMaxDataSlots(LORA_MAX_DATA_SLOTS);

    mesher_ = loramesher::LoraMesher::Builder()
                  .withRadioConfig(radioConfig)
                  .withPinConfig(pinConfig)
                  .withLoRaMeshProtocol(meshConfig)
                  .Build();

    ESP_LOGI(LMS_TAG, "Radio SF=%u BW=%.1f pow=%d maxPkt=%u", (unsigned)LORA_SPREADING_FACTOR,
             (float)LORA_BANDWIDTH, (int)LORA_POWER, (unsigned)LORA_MAX_PACKET_SIZE);

    loraReceiveQueue_ = xQueueCreate(10, sizeof(LoRaQueueMessage*));

    mesher_->SetDataCallback([](loramesher::AddressType source, const std::vector<uint8_t>& data) {
        ESP_LOGV(LMS_TAG, "v2 data callback from %04X, size=%d", source, data.size());

        if (data.size() < sizeof(LoRaMeshMessage)) {
            ESP_LOGW(LMS_TAG, "Received packet too small");
            return;
        }

        const LoRaMeshMessage* lmMsg = reinterpret_cast<const LoRaMeshMessage*>(data.data());
        uint32_t payloadSize = data.size() - sizeof(LoRaMeshMessage);
        DataMessage* dm = (DataMessage*)pvPortMalloc(sizeof(DataMessage) + payloadSize);
        if (!dm) return;

        dm->appPortDst = lmMsg->appPortDst;
        dm->appPortSrc = lmMsg->appPortSrc;
        dm->messageId = lmMsg->messageId;
        dm->addrSrc = source;
        dm->addrDst = LoRaMeshService::getInstance().getLocalAddress();
        dm->messageSize = payloadSize;
        memcpy(dm->message, lmMsg->dataMessage, payloadSize);

        auto* qMsg = new LoRaQueueMessage{source, dm};
        auto& svc = LoRaMeshService::getInstance();
        if (xQueueSend(svc.loraReceiveQueue_, &qMsg, 0) != pdTRUE) {
            vPortFree(dm);
            delete qMsg;
            ESP_LOGW(LMS_TAG, "Receive queue full, dropping packet");
        }
    });

    auto result = mesher_->Start();
    if (result) {
        running_ = true;
        ESP_LOGI(LMS_TAG, "LoraMesher v2 initialized");
        createReceiveTask();
    } else {
        ESP_LOGE(LMS_TAG, "LoraMesher v2 Start failed");
    }
#endif
}

void LoRaMeshService::createReceiveTask() {
    xTaskCreate(loraReceiveLoop, "LoRa_Receive", 8192, nullptr, 2, &loraReceiveTask_Handle);
}

void LoRaMeshService::loraReceiveLoop(void*) {
    auto& svc = LoRaMeshService::getInstance();
    LoRaQueueMessage* qMsg = nullptr;
    for (;;) {
        if (xQueueReceive(svc.loraReceiveQueue_, &qMsg, portMAX_DELAY) == pdTRUE) {
            MessageManager::getInstance().processReceivedMessage(LoRaMeshPort, qMsg->dataMessage);
            vPortFree(qMsg->dataMessage);
            delete qMsg;
            ESP_LOGD(LMS_TAG, "LoRa_Receive stack high water: %u bytes",
                     uxTaskGetStackHighWaterMark(NULL));
        }
    }
}

size_t LoRaMeshService::maxAppPayload() const {
    // LoRaMesher DATA overhead: 6-byte base header and 4-byte data header.
    constexpr size_t LORAMESHER_DATA_OVERHEAD = 10;
    size_t packet = std::min<size_t>(
        LORA_MAX_PACKET_SIZE,
        loramesher::RadioConfig::GetMaxPacketSizeForSf(LORA_SPREADING_FACTOR, LORA_BANDWIDTH));
    return packet - LORAMESHER_DATA_OVERHEAD - sizeof(LoRaMeshMessage);
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
    std::vector<uint8_t> payload(reinterpret_cast<uint8_t*>(loraMeshMessage),
                                 reinterpret_cast<uint8_t*>(loraMeshMessage) + totalSize);

    auto result = mesher_->Send(message->addrDst, payload);
    if (!result) {
        ESP_LOGW(LMS_TAG, "v2 Send failed");
    }

    vPortFree(loraMeshMessage);
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

void LoRaMeshService::standby() {
    if (mesher_) {
        mesher_->Stop();
    }
}

bool LoRaMeshService::hasMeshContact() {
    if (!mesher_)
        return false;
    return mesher_->GetNetworkStatus().connected_nodes > 0 || !mesher_->GetRoutingTable().empty();
}

uint32_t LoRaMeshService::getSuperframeDurationMs() {
    if (!mesher_)
        return 0;
    return mesher_->GetSuperframeDuration();
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
    return mesher_ ? mesher_->GetRxQueueSize() : 0;
}

size_t LoRaMeshService::GetTxQueueSize() const {
    return mesher_ ? mesher_->GetTxQueueSize() : 0;
}
