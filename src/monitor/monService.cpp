#include "monService.h"
#include <Arduino.h>
#include <algorithm>
#include <limits>
#include "devices/initDevices.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "loramesh/loraMeshService.h"
#include "sensor/bme280.h"

static const char* MON_TAG = "MonOMService";

/** @return milliseconds since boot. The clock does not wrap. */
static uint64_t clockMs() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

void MonService::init() {
    ESP_LOGI(MON_TAG, "Initializing mqtt_mon");
    createSendingTask();
}

String MonService::getJSON(DataMessage* message) {
    MonReport report;
    if (!decodeMonReport(message->message, message->messageSize, report)) {
        ESP_LOGW(MON_TAG, "Malformed monitor report from %04X (%u bytes), dropped",
                 message->addrSrc, static_cast<unsigned>(message->messageSize));
        return "";
    }

    // Every key is a string literal, so the document only stores its objects and arrays.
    constexpr size_t REPORT_KEYS = 21;
    constexpr size_t ROUTE_KEYS = 6;
    DynamicJsonDocument doc(JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(REPORT_KEYS) +
                            JSON_ARRAY_SIZE(report.routes.size()) +
                            report.routes.size() * JSON_OBJECT_SIZE(ROUTE_KEYS));
    JsonObject data = doc.createNestedObject("RT");
    message->serialize(data);
    data["RTcount"] = MON_REPORT_MARKER;
    data["uptime"] = monUptimeMs(report);
    data["TxQ"] = report.txQueue;
    data["RxQ"] = report.rxQueue;
    data["number_of_neighbors"] = report.routes.size();
    // The superframe of this gateway, which sizes the wait for a command reply.
    data["sf_ms"] = LoRaMeshService::getInstance().getSuperframeDurationMs();
    if (report.version >= 2) {
        data["v"] = report.version;
        data["interval_s"] = report.intervalS;
        if (report.battMv != 0) data["batt_mv"] = report.battMv;
        data["ext_power"] = report.externalPower ? 1 : 0;
        data["charging"] = report.charging ? 1 : 0;
        data["install"] = report.installMode ? 1 : 0;
    }
    if (report.hasSensor) {
        data["temp_c"] = report.sensor.tempCentiC / 100.0;
        if (report.sensor.rhCentiPercent != MON_NO_HUMIDITY) {
            data["rh"] = report.sensor.rhCentiPercent / 100.0;
        }
        data["hpa"] = report.sensor.pressureDeciHpa / 10.0;
    }
    JsonArray routes = data.createNestedArray("rt");
    for (const MonRoute& route : report.routes) {
        JsonObject entry = routes.createNestedObject();
        entry["neighbor"] = route.neighbor;
        entry["next_hop"] = route.nextHop;
        entry["link_quality"] = route.linkQuality;
        entry["hop_count"] = route.hopCount;
        if (route.rssi != 0) {
            entry["rssi"] = route.rssi;
            entry["snr"] = monSnrDb(route.snrQuarterDb);
        }
    }
    if (doc.overflowed()) {
        ESP_LOGE(MON_TAG, "Monitor report of %u routes from %04X does not fit its JSON document",
                 static_cast<unsigned>(report.routes.size()), message->addrSrc);
        return "";
    }

    String json;
    serializeJson(doc, json);
    return json;
}

DataMessagePtr MonService::getDataMessage(JsonObject data) {
    ESP_LOGW(MON_TAG, "Monitor reports are not accepted from MQTT");
    return nullptr;
}

void MonService::processReceivedMessage(messagePort port, DataMessage* message) {
    ESP_LOGI(MON_TAG, "Received mon data");
}

void MonService::createSendingTask() {
    running = true;
    BaseType_t res =
        xTaskCreatePinnedToCore(sendingLoopOneMessage, /* Function to implement the task */
                                "SendingTask",         /* Name of the task */
                                6000,                  /* Stack size in bytes */
                                nullptr,               /* Task input parameter */
                                1,                     /* Priority of the task */
                                &sending_TaskHandle,   /* Task handle. */
                                0);                    /* Core where the task should run */
    if (res != pdPASS) {
        ESP_LOGE(MON_TAG, "Sending task creation failed");
        running = false;
        return;
    }
    ESP_LOGI(MON_TAG, "Sending task created");
}

std::vector<MonRoute> MonService::collectReportedRoutes() {
    std::vector<MonRoute> routes;
    for (const auto& route : LoRaMeshService::getInstance().getRoutingTableEntries()) {
#ifdef MON_REPORT_ALL_ROUTES
        bool reported = route.is_valid;
#else
        bool reported = route.is_valid && route.destination == route.next_hop;
#endif
        if (!reported) continue;
        MonRoute entry;
        entry.neighbor = route.destination;
        entry.nextHop = route.next_hop;
        entry.linkQuality = route.link_quality;
        entry.hopCount = route.hop_count;
        // The mesh keeps signal readings of direct neighbours only.
        if (route.destination == route.next_hop) {
            entry.rssi = monPackRssi(route.last_rssi);
            entry.snrQuarterDb = monPackSnr(route.last_snr);
        }
        routes.push_back(entry);
    }
    return routes;
}

CommandReply MonService::install(const String& args) {
    uint32_t seconds = 0;
    if (!parseInstallSeconds(args.c_str(), seconds)) return CommandStatus::ARG;
    installMode_.start(seconds, clockMs());
    // The report that follows carries the new state and the new interval.
    if (sending_TaskHandle != nullptr) xTaskNotifyGive(sending_TaskHandle);
    if (seconds == 0) return "OK install off";
    return String(String("OK install ") + seconds + " s");
}

bool MonService::installActive() const {
    return installMode_.active(clockMs());
}

uint32_t MonService::installRemainingS() const {
    return installMode_.remainingS(clockMs());
}

uint32_t MonService::nextReportDelayMs() const {
    uint64_t remainingMs = installMode_.remainingMs(clockMs());
    if (remainingMs == 0) return MON_SENDING_EVERY;
    uint32_t intervalMs =
        InstallMode::reportIntervalMs(LoRaMeshService::getInstance().getSuperframeDurationMs());
    // A report goes out just after the mode ends, so the server sees the normal interval again.
    constexpr uint64_t AFTER_END_MS = 100;
    return static_cast<uint32_t>(std::min<uint64_t>(intervalMs, remainingMs + AFTER_END_MS));
}

MonReport MonService::collectState() const {
    LoRaMeshService& mesh = LoRaMeshService::getInstance();
    MonReport report;
    report.uptimeMs = clockMs();
    report.txQueue = static_cast<uint16_t>(
        std::min<size_t>(mesh.GetTxQueueSize(), std::numeric_limits<uint16_t>::max()));
    report.rxQueue = static_cast<uint16_t>(
        std::min<size_t>(mesh.GetRxQueueSize(), std::numeric_limits<uint16_t>::max()));
    report.installMode = installActive();
    uint32_t intervalMs = report.installMode
                              ? InstallMode::reportIntervalMs(mesh.getSuperframeDurationMs())
                              : MON_SENDING_EVERY;
    report.intervalS = static_cast<uint16_t>(
        std::min<uint32_t>(intervalMs / 1000, std::numeric_limits<uint16_t>::max()));
    Bme280Reading reading;
    if (Bme280Sensor::getInstance().measure(reading)) {
        report.hasSensor = true;
        report.sensor = bme280ToMonSensor(reading);
    }
    PowerState power;
    if (InitDevices::readPower(power)) {
        report.battMv = static_cast<uint16_t>(
            std::min<uint32_t>(power.batteryMv, std::numeric_limits<uint16_t>::max()));
        report.externalPower = power.externalPower;
        report.charging = power.charging;
        report.batteryPresent = power.batteryPresent;
    }
    return report;
}

void MonService::sendReport(const std::vector<MonRoute>& routes) {
    LoRaMeshService& mesh = LoRaMeshService::getInstance();
    MonReport report = collectState();
    report.routes = routes;
    std::vector<MonReport> parts = splitMonReport(report, mesh.maxAppPayload());
    if (parts.empty()) {
        ESP_LOGE(MON_TAG, "A LoRa frame of %u bytes does not hold a monitor report",
                 static_cast<unsigned>(mesh.maxAppPayload()));
        return;
    }

    monMessageId++;
    for (const MonReport& part : parts) {
        std::vector<uint8_t> payload = encodeMonReport(part);

        DataMessagePtr message = allocateDataMessage(payload.size());
        if (!message) {
            ESP_LOGE(MON_TAG, "No memory for a monitor message of %u routes",
                     static_cast<unsigned>(part.routes.size()));
            return;
        }
        message->appPortDst = appPort::MQTTApp;
        message->appPortSrc = appPort::MonApp;
        message->messageId = monMessageId;
        message->addrSrc = mesh.getLocalAddress();
        message->addrDst = 0;
        std::copy(payload.begin(), payload.end(), message->message);
        ESP_LOGV(MON_TAG, "sending monitor report (id/routes/bytes): %u/%u/%u",
                 static_cast<unsigned>(monMessageId), static_cast<unsigned>(part.routes.size()),
                 static_cast<unsigned>(payload.size()));
        MessageManager::getInstance().sendMessage(messagePort::MqttPort, message.get());
    }
}

/** Longest tick wait between two looks at the real clock. */
static constexpr uint32_t WAIT_STEP_MS = 500;

/**
 * @brief Waits @p ms of real time, or until the task is notified.
 *
 * The RTOS tick stands still while the MCU is in light sleep, so a tick timeout grows by the
 * time slept: with 85 % of sleep a 5 minute wait took over half an hour. The wait is cut into
 * short tick waits and the deadline is read from the esp_timer clock, which keeps counting
 * through light sleep.
 *
 * @return true if a notification ended the wait.
 */
static bool waitRealTime(uint32_t ms) {
    const int64_t deadlineUs = esp_timer_get_time() + int64_t{ms} * 1000;
    while (true) {
        const int64_t leftMs = (deadlineUs - esp_timer_get_time()) / 1000;
        if (leftMs <= 0) return false;
        const auto stepMs = static_cast<uint32_t>(std::min<int64_t>(leftMs + 1, WAIT_STEP_MS));
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(stepMs)) != 0) return true;
    }
}

void MonService::sendingLoopOneMessage(void* parameter) {
    MonService& monService = MonService::getInstance();
    ESP_LOGI(MON_TAG, "entering sendingLoop");
    while (true) {
        if (!monService.running) {
            // Wait until a notification to start the task
            ESP_LOGI(MON_TAG, "Wait notification to start the task");
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            ESP_LOGI(MON_TAG, "received notification to start the task");
        } else {
            ESP_LOGD(MON_TAG, "Stack space unused after entering the task: %u",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
            LoRaMeshService::getInstance().updateRoutingTable();
            monService.sendReport(collectReportedRoutes());
            // Static pacing: the offered load is exactly MON_SENDING_EVERY and reproducible
            // (see docs/paper/sf_offered_load_capacity.md). Install mode shortens the wait,
            // and a change of the mode ends it at once.
            waitRealTime(monService.nextReportDelayMs());
            // Print the free heap memory
            ESP_LOGD(MON_TAG, "Free heap: %d", esp_get_free_heap_size());
        }
    }
}

