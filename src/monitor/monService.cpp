#include "monService.h"
#include <Arduino.h>
#include <algorithm>
#include "loramesh/loraMeshService.h"
#ifndef USE_LORAMESHER_V2
#include "LoraMesher.h"
#endif
#include "esp_heap_caps.h"
#include "monServiceMessage.h"

static const char* MON_TAG = "MonOMService";

void MonService::init() {
    ESP_LOGI(MON_TAG, "Initializing mqtt_mon");
    createSendingTask();
}

String MonService::getJSON(DataMessage* message) {
    monMessage* bm = (monMessage*)message;
    DynamicJsonDocument doc(2000);
    JsonObject data = doc.createNestedObject("RT");
    if ((bm->RTcount == MONCOUNT_MONONEMESSAGE) || (bm->messageSize != 17)) {
        ESP_LOGI(MON_TAG, "getJSON: monOneMessage->serialize");
        monOneMessage* mon = (monOneMessage*)message;
        mon->serialize(data);
    } else {
        ESP_LOGI(MON_TAG, "getJSON: monMessage->serialize");
        bm->serialize(data);
    }

    String json;
    serializeJson(doc, json);

    return json;
}

DataMessage* MonService::getDataMessage(JsonObject data) {
    ESP_LOGI(MON_TAG, "getDataMessage");
    if (data["RTcount"] == MONCOUNT_MONONEMESSAGE) {
        // monOneMessage *mon = new monOneMessage();
        ESP_LOGI(MON_TAG, "getDataMessage: monOneMessage");
        // ESP_LOGD(MON_TAG, "getDataMessage: %d", data["messageSize"]);
        int messageSize = atoi(data["messageSize"]);
        monOneMessage* mon = (monOneMessage*)pvPortMalloc(sizeof(DataMessageGeneric) + messageSize);
        mon->deserialize(data);
        mon->messageSize = messageSize;
        return ((DataMessage*)mon);
    }
    monMessage* mon = new monMessage();
    mon->deserialize(data);
    mon->messageSize = sizeof(monMessage) - sizeof(DataMessageGeneric);
    return ((DataMessage*)mon);
}

void MonService::processReceivedMessage(messagePort port, DataMessage* message) {
    ESP_LOGI(MON_TAG, "Received mon data");
}

void MonService::createSendingTask() {
    running = true;
    isCreated = true;
    BaseType_t res = xTaskCreatePinnedToCore(
        sendingLoopOneMessage, /* Function to implement the task */
        "SendingTask",       /* Name of the task */
        6000,                /* Stack size in words */
        NULL,                /* Task input parameter */
        1,                   /* Priority of the task */
        &sending_TaskHandle, /* Task handle. */
        0);                  /* Core where the task should run */
    if (res != pdPASS) {
        ESP_LOGE(MON_TAG, "Sending task creation failed");
        running = false;
        isCreated = false;
        return;
    }
    ESP_LOGI(MON_TAG, "Sending task created");
}

monOneMessage* MonService::createMONPayloadMessage(int number_of_neighbors) {
    uint32_t messageSize = sizeof(monOneMessage) + sizeof(routing_entry) * number_of_neighbors;
    monOneMessage* MONMessage = (monOneMessage*)pvPortMalloc(messageSize);
    if (MONMessage == nullptr) return nullptr;
    MONMessage->messageSize = messageSize - sizeof(DataMessageGeneric);
    MONMessage->RTcount = MONCOUNT_MONONEMESSAGE;
    MONMessage->uptime = millis();
#ifdef USE_LORAMESHER_V2
    MONMessage->TxQ = LoRaMeshService::getInstance().GetTxQueueSize();
    MONMessage->RxQ = LoRaMeshService::getInstance().GetRxQueueSize();
#else
    MONMessage->TxQ = LoraMesher::getInstance().getSendQueueSize();
    MONMessage->RxQ = LoraMesher::getInstance().getReceivedQueueSize();
#endif
    MONMessage->number_of_neighbors = number_of_neighbors;
    MONMessage->appPortDst = appPort::MQTTApp;
    MONMessage->appPortSrc = appPort::MonApp;
    MONMessage->addrSrc = LoRaMeshService::getInstance().getLocalAddress();
    MONMessage->addrDst = 0;
    MONMessage->messageId = monMessageId;
    return MONMessage;
}

#ifdef USE_LORAMESHER_V2
std::vector<routing_entry> MonService::collectReportedRoutes() {
    std::vector<routing_entry> entries;
    for (const auto& route : LoRaMeshService::getInstance().getRoutingTableEntries()) {
#ifdef MON_REPORT_ALL_ROUTES
        bool reported = route.is_valid;
#else
        bool reported = route.is_valid && route.destination == route.next_hop;
#endif
        if (!reported) continue;
        routing_entry entry;
        entry.neighbor = route.destination;
        entry.next_hop = route.next_hop;
        entry.link_quality = route.link_quality;
        entry.hop_count = route.hop_count;
        entries.push_back(entry);
    }
    return entries;
}

void MonService::sendRoutes(const std::vector<routing_entry>& entries) {
    size_t perMessage = 1;
    while (getOneMessageSize(perMessage + 1) < MAX_MSG_SIZE) ++perMessage;

    monMessageId++;
    for (size_t first = 0; first < entries.size(); first += perMessage) {
        size_t count = std::min(perMessage, entries.size() - first);
        monOneMessage* message = createMONPayloadMessage(count);
        if (message == nullptr) {
            ESP_LOGE(MON_TAG, "No memory for a monitor message of %u routes", count);
            return;
        }
        std::copy(entries.begin() + first, entries.begin() + first + count, message->rt);
        ESP_LOGV(MON_TAG, "sending monOneMessage (MessageId/routes/bytes): %d/%u/%d",
                 monMessageId, count, getOneMessageSize(count));
        MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*)message);
        vPortFree(message);
    }
}
#endif

void MonService::sendingLoopOneMessage(void* parameter) {
    MonService& monService = MonService::getInstance();
    UBaseType_t uxHighWaterMark;
    ESP_LOGI(MON_TAG, "entering sendingLoop");
    while (true) {
        if (!monService.running) {
            // Wait until a notification to start the task
            ESP_LOGI(MON_TAG, "Wait notification to start the task");
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            ESP_LOGI(MON_TAG, "received notification to start the task");
        } else {
            uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGD(MON_TAG, "Stack space unused after entering the task: %d", uxHighWaterMark);
#ifdef USE_LORAMESHER_V2
            LoRaMeshService::getInstance().updateRoutingTable();
            std::vector<routing_entry> entries = collectReportedRoutes();
            if (entries.empty()) {
                ESP_LOGD(MON_TAG, "sendingLoopOneMessage: no neighbors?");
            } else {
                monService.sendRoutes(entries);
            }
#else
            RoutingTableService::printRoutingTable();
            LoRaMeshService::getInstance().updateRoutingTable();
            LM_LinkedList<RouteNode>* routingTableList =
                LoRaMeshService::getInstance().routingTableList;
            // count neighbors
            if (routingTableList->moveToStart()) {
                routingTableList->setInUse();
                MonService::getInstance().monMessageId++;
                uint16_t monMessagecount = 0;
                do {
                    RouteNode* rtn = routingTableList->getCurrent();
                    if (rtn->networkNode.address == rtn->via) {
                        ++monMessagecount;
                    };
                } while (routingTableList->next());
                if (monMessagecount > 0) {
                    routingTableList->moveToStart();
                    monOneMessage* MONMessage =
                        getInstance().createMONPayloadMessage(monMessagecount);
                    int i = 0;
                    do {
                        RouteNode* rtn = routingTableList->getCurrent();
                        if (rtn->networkNode.address == rtn->via) {
                            routing_entry& entry = MONMessage->rt[i++];
                            entry.neighbor = rtn->networkNode.address;
                            entry.next_hop = rtn->via;
                            entry.link_quality = 0;
                            entry.hop_count = rtn->networkNode.metric;
                        }
                    } while (routingTableList->next());
                    ESP_LOGV(MON_TAG, "sending monOneMessage");
                    // Send the message
                    MessageManager::getInstance().sendMessage(messagePort::MqttPort,
                                                              (DataMessage*)MONMessage);
                    // Delete the message
                    vPortFree(MONMessage);
                } else {
                    ESP_LOGD(MON_TAG, "sendingLoopOneMessage: no neighbors?");
                }
            } else {
                ESP_LOGD(MON_TAG, "No routes");
            }
#endif
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

