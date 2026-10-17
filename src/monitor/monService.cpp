#include "monService.h"
#include <Arduino.h>
#include <algorithm>
#include "esp_heap_caps.h"
#include "loramesh/loraMeshService.h"

static const char* MON_TAG = "MonOMService";

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

    DynamicJsonDocument doc(2000);
    JsonObject data = doc.createNestedObject("RT");
    message->serialize(data);
    data["RTcount"] = MON_REPORT_MARKER;
    data["uptime"] = report.uptimeMs;
    data["TxQ"] = report.txQueue;
    data["RxQ"] = report.rxQueue;
    data["number_of_neighbors"] = report.routes.size();
    JsonArray routes = data.createNestedArray("rt");
    for (const MonRoute& route : report.routes) {
        JsonObject entry = routes.createNestedObject();
        entry["neighbor"] = route.neighbor;
        entry["next_hop"] = route.nextHop;
        entry["link_quality"] = route.linkQuality;
        entry["hop_count"] = route.hopCount;
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
    isCreated = true;
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
        isCreated = false;
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
        routes.push_back(entry);
    }
    return routes;
}

void MonService::sendRoutes(const std::vector<MonRoute>& routes) {
    LoRaMeshService& mesh = LoRaMeshService::getInstance();
    size_t perMessage = monRoutesPerMessage(mesh.maxAppPayload());
    if (perMessage == 0) {
        ESP_LOGE(MON_TAG, "A LoRa frame of %u bytes holds no route",
                 static_cast<unsigned>(mesh.maxAppPayload()));
        return;
    }

    monMessageId++;
    for (size_t first = 0; first < routes.size(); first += perMessage) {
        size_t count = std::min(perMessage, routes.size() - first);
        MonReport report;
        report.uptimeMs = millis();
        report.txQueue = static_cast<uint16_t>(mesh.GetTxQueueSize());
        report.rxQueue = static_cast<uint16_t>(mesh.GetRxQueueSize());
        report.routes.assign(routes.begin() + static_cast<std::ptrdiff_t>(first),
                             routes.begin() + static_cast<std::ptrdiff_t>(first + count));
        std::vector<uint8_t> payload = encodeMonReport(report);

        DataMessagePtr message = allocateDataMessage(payload.size());
        if (!message) {
            ESP_LOGE(MON_TAG, "No memory for a monitor message of %u routes",
                     static_cast<unsigned>(count));
            return;
        }
        message->appPortDst = appPort::MQTTApp;
        message->appPortSrc = appPort::MonApp;
        message->messageId = monMessageId;
        message->addrSrc = mesh.getLocalAddress();
        message->addrDst = 0;
        std::copy(payload.begin(), payload.end(), message->message);
        ESP_LOGV(MON_TAG, "sending monitor report (id/routes/bytes): %u/%u/%u",
                 static_cast<unsigned>(monMessageId), static_cast<unsigned>(count),
                 static_cast<unsigned>(payload.size()));
        MessageManager::getInstance().sendMessage(messagePort::MqttPort, message.get());
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
            std::vector<MonRoute> routes = collectReportedRoutes();
            if (routes.empty()) {
                ESP_LOGD(MON_TAG, "sendingLoopOneMessage: no neighbors?");
            } else {
                monService.sendRoutes(routes);
            }
            // end send MON
            // Static pacing: fixed MON_SENDING_EVERY delay. Offered load is exactly
            // this interval and reproducible; per-SF capacity matching is done by
            // choosing MON_SENDING_EVERY (see docs/paper/sf_offered_load_capacity.md).
            vTaskDelay(MON_SENDING_EVERY / portTICK_PERIOD_MS);
            // Print the free heap memory
            ESP_LOGD(MON_TAG, "Free heap: %d", esp_get_free_heap_size());
        }
    }
}

