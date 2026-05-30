#if !defined(USE_LORAMESHER_V2)
#define USE_LORAMESHER_V2
#endif
#include "monService.h"
#include <Arduino.h>
#include "loramesh/loraMeshService.h"
#ifndef USE_LORAMESHER_V2
#include "LoraMesher.h"
#endif
#include "esp_heap_caps.h"
#include "monServiceMessage.h"

#if defined(MON_MQTT_ONE_MESSAGE)
static const char* MON_TAG = "MonOMService";
#else
static const char* MON_TAG = "MonService";
#endif

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
#if defined(MON_MQTT_ONE_MESSAGE)
        sendingLoopOneMessage, /* Function to implement the task */
#else
        sendingLoop, /* Function to implement the task */
#endif
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

#if defined(MON_MQTT_ONE_MESSAGE)

monOneMessage* MonService::createMONPayloadMessage(int number_of_neighbors) {
    uint32_t messageSize = sizeof(monOneMessage) + sizeof(routing_entry) * number_of_neighbors;
    monOneMessage* MONMessage = (monOneMessage*)pvPortMalloc(messageSize);
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
            auto routes = LoRaMeshService::getInstance().getRoutingTableEntries();
            uint16_t routeCount = 0;
            for (const auto& route : routes) {
#ifdef MON_REPORT_ALL_ROUTES
                if (route.is_valid) {
#else
                // if (route.is_valid && route.destination == route.next_hop) {
                if (route.destination == route.next_hop) {
#endif
                    ++routeCount;
                }
            }
            if (routeCount > 0) {
                monOneMessage* MONMessage = nullptr;
                int routeSent = 0;  // routes sent in previous messages
                int routeNext = 0;  // routes to put in next message
                int i = 0;
                MonService::getInstance().monMessageId++;
                for (const auto& route : routes) {
                    if ((routeNext == 0 && routeSent < routeCount) ||
                        MONMessage == nullptr) {  // create a new message
                        routeNext = 1;
                        while ((MonService::getOneMessageSize(routeNext + 1) < MAX_MSG_SIZE) &&
                               (routeNext < (routeCount - routeSent)))
                            ++routeNext;
                        MONMessage = getInstance().createMONPayloadMessage(routeNext);
                    }
#ifdef MON_REPORT_ALL_ROUTES
                    if (route.is_valid) {
#else
                    if (route.is_valid && route.destination == route.next_hop) {
#endif
                        routing_entry entry;
                        entry.neighbor = route.destination;
                        entry.next_hop = route.next_hop;
                        entry.link_quality = route.link_quality;
                        entry.hop_count = route.hop_count;
                        // entry.RxSNR = static_cast<int8_t>(route.link_quality / 2 - 64);
                        // entry.SRTT = route.last_seen_ms;
                        // Approximate SNR from link_quality: lq/2 - 64
                        // entry.RxSNR = static_cast<int8_t>(route.link_quality / 2 - 64);
                        // entry.SRTT = route.last_seen_ms;
                        MONMessage->rt[i] = entry;
                        i += 1;
                        routeSent += 1;
                        if (i == routeNext) {  // send the message
                            ESP_LOGV(MON_TAG,
                                     "sending monOneMessage (MessageId/routes/bytes): %d/%d/%d",
                                     MonService::getInstance().monMessageId, routeNext,
                                     MonService::getOneMessageSize(routeNext));
                            routeNext = 0;
                            i = 0;
                            MessageManager::getInstance().sendMessage(messagePort::MqttPort,
                                                                      (DataMessage*)MONMessage);
                            vPortFree(MONMessage);
                            MONMessage = nullptr;
                        }
                    }
                }
            } else {
                ESP_LOGD(MON_TAG, "sendingLoopOneMessage: no neighbors?");
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
                            MONMessage->rt[i++] = {rtn->networkNode.address, rtn->via, 0,
                                                   rtn->networkNode.metric};
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
            // #ifdef USE_LORAMESHER_V2
            //             {
            //                 uint32_t delay_ms =
            //                     LoRaMeshService::getInstance().getTimeUntilNextDataSlot();
            //                 if (delay_ms == 0)
            //                     delay_ms = MON_SENDING_EVERY;
            //                 vTaskDelay(delay_ms / portTICK_PERIOD_MS);
            //             }
            // #else
            vTaskDelay(MON_SENDING_EVERY / portTICK_PERIOD_MS);
            // #endif
            // Print the free heap memory
            ESP_LOGD(MON_TAG, "Free heap: %d", esp_get_free_heap_size());
        }
    }
}

#else

void MonService::sendingLoop(void* parameter) {
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
            // send MON, one entry per mqtt message

#ifdef USE_LORAMESHER_V2
            LoRaMeshService::getInstance().updateRoutingTable();
            auto routes = LoRaMeshService::getInstance().getRoutingTableEntries();
            if (!routes.empty()) {
                MonService::getInstance().monMessageId++;
                uint16_t monMessagecount = 0;
                for (const auto& route : routes) {
                    if (route.is_valid) {
                        getInstance().createAndSendMessage(++monMessagecount, route);
                    }
                }
            } else {
                ESP_LOGD(MON_TAG, "No routes");
            }
#else
            RoutingTableService::printRoutingTable();
            LM_LinkedList<RouteNode>* routingTableList =
                LoRaMeshService::getInstance().radio.routingTableListCopy();
            routingTableList->setInUse();
            if (routingTableList->moveToStart()) {
                MonService::getInstance().monMessageId++;
                uint16_t monMessagecount = 0;
                do {
                    RouteNode* rtn = routingTableList->getCurrent();
                    getInstance().createAndSendMessage(++monMessagecount, rtn);
                } while (routingTableList->next());
            } else {
                ESP_LOGD(MON_TAG, "No routes");
            }
            // Release routing table list usage.
            routingTableList->releaseInUse();
            routingTableList->Clear();
#endif
            // end send MON
            // #ifdef USE_LORAMESHER_V2
            //             {
            //                 uint32_t delay_ms =
            //                     LoRaMeshService::getInstance().getTimeUntilNextDataSlot();
            //                 if (delay_ms == 0)
            //                     delay_ms = MON_SENDING_EVERY;
            //                 vTaskDelay(delay_ms / portTICK_PERIOD_MS);
            //             }
            // #else
            vTaskDelay(MON_SENDING_EVERY / portTICK_PERIOD_MS);
            // #endif
            // Print the free heap memory
            ESP_LOGD(MON_TAG, "Free heap: %d", esp_get_free_heap_size());
        }
    }
}

#ifdef USE_LORAMESHER_V2
void MonService::createAndSendMessage(uint16_t mcount, const loramesher::RouteEntry& route) {
    ESP_LOGV(MON_TAG, "Sending mon data %d", MonService::getInstance().monMessageId);
    monMessage* message = new monMessage();
    message->appPortDst = appPort::MQTTApp;
    message->appPortSrc = appPort::MonApp;
    message->messageId = monMessageId;
    message->addrSrc = LoRaMeshService::getInstance().getLocalAddress();
    message->addrDst = 0;
    //
    message->RTcount = mcount;
    message->address = route.destination;
    message->metric = route.hop_count;
    message->via = route.next_hop;
    // Approximate SNR from link_quality
    message->receivedSNR = static_cast<int8_t>(route.link_quality / 2 - 64);
    message->sentSNR = 0;
    message->SRTT = route.last_seen_ms;
    message->RTTVAR = 0;
    ESP_LOGV(MON_TAG, "routing table");
    message->messageSize = sizeof(monMessage) - sizeof(DataMessageGeneric);
    // Send the message
    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*)message);
    // Delete the message
    delete message;
}
#else
void MonService::createAndSendMessage(uint16_t mcount, RouteNode* rtn) {
    ESP_LOGV(MON_TAG, "Sending mon data %d", MonService::getInstance().monMessageId);
    monMessage* message = new monMessage();
    message->appPortDst = appPort::MQTTApp;
    message->appPortSrc = appPort::MonApp;
    message->messageId = monMessageId;
    message->addrSrc = LoraMesher::getInstance().getLocalAddress();
    message->addrDst = 0;
    //
    message->RTcount = mcount;
    message->address = rtn->networkNode.address;
    message->metric = rtn->networkNode.metric;
    message->via = rtn->via;
    message->receivedSNR = rtn->receivedSNR;
    message->sentSNR = rtn->sentSNR;
    message->SRTT = rtn->SRTT;
    message->RTTVAR = rtn->RTTVAR;
    ESP_LOGV(MON_TAG, "routing table");
    message->messageSize = sizeof(monMessage) - sizeof(DataMessageGeneric);
    // Send the message
    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*)message);
    // Delete the message
    delete message;
}
#endif

#endif
