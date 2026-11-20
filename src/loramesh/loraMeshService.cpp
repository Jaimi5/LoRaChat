#include "loraMeshService.h"

#include "devices/initDevices.h"
#include "monReport.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "soc/rtc_cntl_reg.h"

static const char* LMS_TAG = "LoRaMeshService";
static constexpr UBaseType_t RECEIVE_QUEUE_LENGTH = 10;
static_assert(sizeof(LoRaMeshMessage) == 3, "LoRa frame header: port dst, port src, id");

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

/** @return milliseconds since boot. The clock keeps running through light sleep. */
static uint32_t clockMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

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

void LoRaMeshService::sleepCountsInto(Fields& out, size_t first) const {
    enum Field : size_t { LIGHT_COUNT, LIGHT_SECONDS, DEEP_COUNT, DEEP_SECONDS, KEY_WAKES };
    constexpr uint32_t MS_PER_S = 1000;
    out.setU(first + LIGHT_COUNT, sleepStats.lightCount);
    out.setU(first + LIGHT_SECONDS, sleepStats.lightMs / MS_PER_S);
    out.setU(first + DEEP_COUNT, sleepStats.deepCount);
    out.setU(first + DEEP_SECONDS, sleepStats.deepMs / MS_PER_S);
#if PMU_KEY_WAKES_LIGHT_SLEEP
    out.setU(first + KEY_WAKES, keyWakes_.load());
#endif
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
    InitDevices::armPowerKeyWake(!deep);
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
    // An edge of the PMU interrupt line is lost while the MCU sleeps: look at the line now.
    InitDevices::checkPowerKey();
    if (previous != loramesher::power::PowerState::LIGHT_SLEEP) return;
#if PMU_KEY_WAKES_LIGHT_SLEEP
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) keyWakes_++;
#endif
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

    if (!loraReceiveQueue_.create(RECEIVE_QUEUE_LENGTH)) {
        ESP_LOGE(LMS_TAG, "No memory for the receive queue");
        return;
    }

    mesher_->SetDataCallback([](loramesher::AddressType source, const std::vector<uint8_t>& data) {
        ESP_LOGV(LMS_TAG, "v2 data callback from %04X, size=%d", source, data.size());
        LoRaMeshService::getInstance().lastRxMs_ = std::max<uint32_t>(clockMs(), 1);

        if (data.size() < sizeof(LoRaMeshMessage)) {
            ESP_LOGW(LMS_TAG, "Received packet too small");
            return;
        }

        const LoRaMeshMessage* lmMsg = reinterpret_cast<const LoRaMeshMessage*>(data.data());
        size_t payloadSize = data.size() - sizeof(LoRaMeshMessage);
        DataMessagePtr dm = allocateDataMessage(payloadSize);
        if (!dm) {
            ESP_LOGW(LMS_TAG, "No memory for a received packet, dropped");
            return;
        }

        auto& svc = LoRaMeshService::getInstance();
        dm->appPortDst = lmMsg->appPortDst;
        dm->appPortSrc = lmMsg->appPortSrc;
        dm->messageId = lmMsg->messageId;
        dm->addrSrc = source;
        dm->addrDst = svc.getLocalAddress();
        memcpy(dm->message, lmMsg->dataMessage, payloadSize);

        if (!svc.loraReceiveQueue_.send(dm, 0)) {
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
    for (;;) {
        DataMessagePtr message = svc.loraReceiveQueue_.receive(portMAX_DELAY);
        if (!message) continue;
        MessageManager::getInstance().processReceivedMessage(LoRaMeshPort, message.get());
        ESP_LOGD(LMS_TAG, "LoRa_Receive stack high water: %u bytes",
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
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

TypedResult LoRaMeshService::routesInto(Fields& out) {
    enum Field : size_t { COUNT, ROUTES };
    enum Member : size_t { DESTINATION, NEXT_HOP, LINK_QUALITY, HOPS, RSSI, SNR };
    constexpr int32_t CENTI_DB_PER_QUARTER_DB = 25;
    auto setAddress = [](Fields& row, size_t index, uint16_t address) {
        const uint8_t bytes[] = {static_cast<uint8_t>(address >> 8),
                                 static_cast<uint8_t>(address & 0xFF)};
        row.setBytes(index, bytes, sizeof(bytes));
    };
    std::vector<loramesher::RouteEntry> routes;
    if (mesher_) routes = mesher_->GetRoutingTable();
    out.setU(COUNT, static_cast<uint32_t>(routes.size()));
    for (const auto& route : routes) {
        Fields row;
        setAddress(row, DESTINATION, route.destination);
        setAddress(row, NEXT_HOP, route.next_hop);
        row.setU(LINK_QUALITY, route.link_quality);
        row.setU(HOPS, route.hop_count);
        // The mesh keeps signal readings of direct neighbours only.
        const int8_t rssi = route.destination == route.next_hop ? monPackRssi(route.last_rssi) : 0;
        if (rssi != 0) {
            row.setU(RSSI, static_cast<uint32_t>(-rssi));
            row.setI(SNR, monPackSnr(route.last_snr) * CENTI_DB_PER_QUARTER_DB);
        }
        out.addRow(row);
    }
    return TypedResult();
}

MeshSend LoRaMeshService::send(DataMessage* message) {
    if (!mesher_) return MeshSend::FAILED;

    ESP_LOGV(LMS_TAG, "Heap size send: %d", ESP.getFreeHeap());

    auto result = mesher_->Send(message->addrDst, createLoRaMeshFrame(*message));
    if (result) return MeshSend::SENT;
    ESP_LOGW(LMS_TAG, "Send to %04X failed: %s", message->addrDst,
             result.GetErrorMessage().c_str());
    switch (result.getErrorCode()) {
        case loramesher::LoraMesherErrorCode::kNoRoute:
            return MeshSend::NO_ROUTE;
        case loramesher::LoraMesherErrorCode::kQueueFull:
            return MeshSend::QUEUE_FULL;
        default:
            return MeshSend::FAILED;
    }
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
    return send(message) == MeshSend::SENT;
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

RouteState LoRaMeshService::routeStateOf(uint16_t dst) {
    RouteState state;
    for (const auto& route : getRoutingTableEntries()) {
        if (route.destination != dst) continue;
        state.listed = true;
        state.active = route.is_valid;
        state.hops = route.hop_count;
        state.nextHop = route.next_hop;
        break;
    }
    return state;
}

bool LoRaMeshService::timeToNextTxSlotMs(uint32_t& waitMs) {
    if (!running_) return false;
    // The library subtracts the guard from the wait and answers 0 only without a TX slot. A
    // slot closer than the guard counts as its next turn.
    constexpr uint32_t GUARD_MS =
        loramesher::protocols::LoRaMeshProtocol::kDefaultDataSlotGuardTimeMs;
    uint32_t untilSlotMs = mesher_->GetTimeUntilNextDataSlot(GUARD_MS);
    if (untilSlotMs == 0) return false;

    using loramesher::types::protocols::lora_mesh::SlotAllocation;
    TxSlots slots;
    slots.superframeMs = mesher_->GetSuperframeDuration();
    // The table lists every slot of the superframe, all of the same duration.
    std::vector<SlotAllocation> table = mesher_->GetSlotTable();
    uint32_t slotMs = table.empty() ? 0 : slots.superframeMs / static_cast<uint32_t>(table.size());
    for (const SlotAllocation& slot : table) {
        if (slot.type == SlotAllocation::SlotType::TX) {
            slots.startsMs.push_back(slot.slot_number * slotMs);
        }
    }
    waitMs = queuedSlotWaitMs(
        static_cast<uint32_t>(std::min<uint64_t>(uint64_t{untilSlotMs} + GUARD_MS, UINT32_MAX)),
        mesher_->GetTxQueueSize(), slots, GUARD_MS);
    return true;
}

void LoRaMeshService::updateRoutingTable() {
    // v2 routing table is always fresh from GetRoutingTable()
    if (mesher_ && mesher_->GetRoutingTable().empty()) {
        ESP_LOGW(LMS_TAG, "No routes in the routing table");
    }
}

std::vector<loramesher::RouteEntry> LoRaMeshService::getRoutingTableEntries() {
    if (!running_) return {};
    return mesher_->GetRoutingTable();
}

MeshStatus LoRaMeshService::getMeshStatus() {
    using loramesher::types::protocols::lora_mesh::ProtocolState;
    static_assert(
        static_cast<int>(ProtocolState::INITIALIZING) ==
                static_cast<int>(MeshState::INITIALIZING) &&
            static_cast<int>(ProtocolState::DISCOVERY) == static_cast<int>(MeshState::DISCOVERY) &&
            static_cast<int>(ProtocolState::JOINING) == static_cast<int>(MeshState::JOINING) &&
            static_cast<int>(ProtocolState::NORMAL_OPERATION) ==
                static_cast<int>(MeshState::NORMAL_OPERATION) &&
            static_cast<int>(ProtocolState::NETWORK_MANAGER) ==
                static_cast<int>(MeshState::NETWORK_MANAGER) &&
            static_cast<int>(ProtocolState::FAULT_RECOVERY) ==
                static_cast<int>(MeshState::FAULT_RECOVERY) &&
            static_cast<int>(ProtocolState::NM_ELECTION) ==
                static_cast<int>(MeshState::NM_ELECTION),
        "MeshState follows the order of the LoRaMesher ProtocolState");
    MeshStatus status;
    if (!running_) return status;
    loramesher::NetworkStatus network = mesher_->GetNetworkStatus();
    status.state = static_cast<MeshState>(network.current_state);
    status.synchronized = network.is_synchronized;
    status.connectedNodes = network.connected_nodes;
    return status;
}

bool LoRaMeshService::getGatewayRoute(uint16_t& nextHop, uint8_t& hops) {
    if (!running_) return false;
    auto gateway = mesher_->GetClosestGateway();
    if (!gateway.has_value()) return false;
    nextHop = gateway->next_hop;
    hops = gateway->hop_count;
    return true;
}

bool LoRaMeshService::getLastRxAgeMs(uint32_t& ageMs) const {
    uint32_t lastRxMs = lastRxMs_;
    if (lastRxMs == 0) return false;
    ageMs = clockMs() - lastRxMs;
    return true;
}

bool LoRaMeshService::getLastHeardAgeMs(const std::vector<loramesher::RouteEntry>& routes,
                                        uint32_t& ageMs) {
    uint32_t nowMs = clockMs();
    uint32_t heardMs = lastHeardMs_;
    bool heard = heardMs != 0;
    uint32_t age = heard ? nowMs - heardMs : 0;
    auto note = [&heard, &age](uint32_t candidate) {
        if (heard && age <= candidate) return;
        heard = true;
        age = candidate;
    };
    uint32_t rxAgeMs = 0;
    if (getLastRxAgeMs(rxAgeMs)) note(rxAgeMs);
    // A neighbour that went silent keeps the time it was last heard.
    for (const auto& route : routes) {
        if (route.destination == route.next_hop) note(route.last_seen_ms);
    }
    if (!heard) return false;
    // Remembered, so the answer outlives the routes it came from.
    lastHeardMs_ = std::max<uint32_t>(nowMs - std::min(age, nowMs), 1);
    ageMs = age;
    return true;
}

std::vector<uint8_t> LoRaMeshService::createLoRaMeshFrame(const DataMessage& message) {
    std::vector<uint8_t> frame;
    frame.reserve(sizeof(LoRaMeshMessage) + message.messageSize);
    frame.push_back(message.appPortDst);
    frame.push_back(message.appPortSrc);
    frame.push_back(message.messageId);
    frame.insert(frame.end(), message.message, message.message + message.messageSize);
    return frame;
}

size_t LoRaMeshService::GetRxQueueSize() const {
    return mesher_ ? mesher_->GetRxQueueSize() : 0;
}

size_t LoRaMeshService::GetTxQueueSize() const {
    return mesher_ ? mesher_->GetTxQueueSize() : 0;
}
