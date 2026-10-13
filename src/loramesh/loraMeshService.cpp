#include "loraMeshService.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"

static const char* LMS_TAG = "LoRaMeshService";

// Sleeps since power-on, in RTC memory so deep sleeps add up. A deep sleep is booked on the
// next boot: the library can still call it off after the callback allowed it.
struct SleepStats {
    uint32_t magic;
    uint32_t lightCount;
    uint32_t lightMs;
    uint32_t deepCount;
    uint32_t deepMs;
    uint32_t deepPending;
    uint32_t deepPlannedMs;
};
static constexpr uint32_t SLEEP_STATS_MAGIC = 0x534C5031;  // "SLP1"
RTC_DATA_ATTR static SleepStats sleepStats;

void LoRaMeshService::releaseDeepSleepPadHold() {
    // The digital pads latched at the deep sleep stay held after the wake-up until released.
    gpio_deep_sleep_hold_dis();
    SET_PERI_REG_MASK(RTC_CNTL_DIG_ISO_REG, RTC_CNTL_CLR_DG_PAD_AUTOHOLD);
}

void LoRaMeshService::countDeepSleepWake() {
    if (sleepStats.magic != SLEEP_STATS_MAGIC) {
        sleepStats = SleepStats{};
        sleepStats.magic = SLEEP_STATS_MAGIC;
    }
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP && sleepStats.deepPending) {
        sleepStats.deepCount++;
        sleepStats.deepMs += sleepStats.deepPlannedMs;
    }
    sleepStats.deepPending = 0;
}

std::string LoRaMeshService::sleepReport() const {
    char text[96];
    snprintf(text, sizeof(text), "light %u x %u s, deep %u x %u s since power-on",
             (unsigned)sleepStats.lightCount, (unsigned)(sleepStats.lightMs / 1000),
             (unsigned)sleepStats.deepCount, (unsigned)(sleepStats.deepMs / 1000));
    return text;
}

loramesher::power::SleepResult LoRaMeshService::onPrepareSleep(
    const loramesher::power::SleepContext& context) {
    bool deep = context.requested_state == loramesher::power::PowerState::DEEP_SLEEP;
    SleepBlockers blockers = sleepBlockers_ ? sleepBlockers_() : SleepBlockers{};
    SleepDecision decision = decideSleep(sleepMode_, deep, blockers);
    // Logged on change only: each log line blocks the mesh task on the UART.
    if (decision != lastSleepDecision_ && decision != SleepDecision::NO_DEEP_SLEEP) {
        ESP_LOGI(LMS_TAG, "MCU sleep: %s", sleepDecisionName(decision));
        lastSleepDecision_ = decision;
    }
    if (decision != SleepDecision::ALLOW) return loramesher::power::SleepResult{false};
    if (deep) {
        // The radio's reset and chip-select lines keep their levels through the deep sleep, so
        // the radio stays asleep. The hold only acts in deep sleep, so a cancelled one is harmless.
        gpio_deep_sleep_hold_en();
        sleepStats.deepPending = 1;
        sleepStats.deepPlannedMs = context.sleep_duration_ms;
        Serial.flush();
    } else {
        lightSleepPlannedMs_ = context.sleep_duration_ms;
    }
    return loramesher::power::SleepResult{true};
}

void LoRaMeshService::onWakeUp(loramesher::power::PowerState previous) {
    if (previous != loramesher::power::PowerState::LIGHT_SLEEP) return;
    sleepStats.lightCount++;
    sleepStats.lightMs += lightSleepPlannedMs_;
}


void LoRaMeshService::initLoraMesherService(bool networkManager, SleepMode sleepMode,
                                            int sleepFractionPercent) {
#ifdef LORA_ENABLED
    countDeepSleepWake();

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
    meshConfig.setMinSleepFraction(sleepFractionPercent >= 0 ? sleepFractionPercent / 100.0f
                                                             : LORA_MIN_SLEEP_FRACTION);
    meshConfig.setDefaultDataSlots(LORA_DEFAULT_DATA_SLOTS);
    meshConfig.setMaxNetworkNodes(LORA_MAX_NETWORK_NODES);
    meshConfig.setMaxDataSlots(LORA_MAX_DATA_SLOTS);

    loramesher::LoraMesher::Builder builder;
    builder.withRadioConfig(radioConfig).withPinConfig(pinConfig).withLoRaMeshProtocol(meshConfig);

    // The MCU only sleeps with a prepare-sleep callback; the network manager never sleeps.
    sleepMode_ = networkManager ? SleepMode::NONE : sleepMode;
    if (sleepMode_ != SleepMode::NONE) {
        builder
            .withPrepareSleepCallback([](const loramesher::power::SleepContext& context) {
                return LoRaMeshService::getInstance().onPrepareSleep(context);
            })
            .withWakeUpCallback([](loramesher::power::PowerState previous) {
                LoRaMeshService::getInstance().onWakeUp(previous);
            });
    }
    if (sleepMode_ == SleepMode::DEEP) {
        // RTC memory keeps the mesh state through deep sleep, so the node resumes its slots
        loramesher::power::DeepSleepPolicy policy;
        policy.enabled = true;
        policy.min_sleep_ms = LORA_DEEP_SLEEP_MIN_MS;
        policy.boot_time_ms = LORA_DEEP_SLEEP_BOOT_MS;
        builder.withStateStore(std::make_shared<loramesher::storage::RtcStateStore>())
            .withDeepSleep(policy);
    }
    mesher_ = builder.Build();

    ESP_LOGI(LMS_TAG, "Radio SF=%u BW=%.1f pow=%d maxPkt=%u, MCU sleep %s, sleep fraction %d %%",
             (unsigned)LORA_SPREADING_FACTOR, (float)LORA_BANDWIDTH, (int)LORA_POWER,
             (unsigned)LORA_MAX_PACKET_SIZE, sleepModeName(sleepMode_),
             sleepFractionPercent >= 0 ? sleepFractionPercent
                                       : (int)(LORA_MIN_SLEEP_FRACTION * 100));

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
