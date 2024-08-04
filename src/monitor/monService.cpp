#include <Arduino.h>
#include "monServiceMessage.h"
#include "monService.h"
#include "loramesh/loraMeshService.h"
#include "LoraMesher.h"

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
  monMessage* bm = (monMessage*) message;
  StaticJsonDocument<2000> doc;
  JsonObject data = doc.createNestedObject("RT");
  if ((bm->RTcount == MONCOUNT_MONONEMESSAGE) || (bm->messageSize != 17)) {
    ESP_LOGI(MON_TAG, "getJSON: monOneMessage->serialize");
    monOneMessage* mon = (monOneMessage*) message;
    mon->serialize(data);
  }
  else {
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
    monOneMessage* mon = (monOneMessage*) pvPortMalloc(sizeof(DataMessageGeneric) + messageSize);
    mon->deserialize(data);
    mon->messageSize = messageSize;
    return ((DataMessage*) mon);
  }
  monMessage* mon = new monMessage();
  mon->deserialize(data);
  mon->messageSize = sizeof(monMessage) - sizeof(DataMessageGeneric);
  return ((DataMessage*) mon);
}

void MonService::processReceivedMessage(messagePort port, DataMessage* message) {
  ESP_LOGI(MON_TAG, "Received mon data");
}

void MonService::createSendingTask() {
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
    0); /* Core where the task should run */
  if (res != pdPASS) {
    ESP_LOGE(MON_TAG, "Sending task creation failed");
    return;
  }
  ESP_LOGI(MON_TAG, "Sending task created");
  running = true;
  isCreated = true;
}

#if defined(MON_MQTT_ONE_MESSAGE)

monOneMessage* MonService::createMONPayloadMessage(int number_of_neighbors) {
  uint32_t messageSize = sizeof(monOneMessage) + sizeof(routing_entry) * number_of_neighbors;
  monOneMessage* MONMessage = (monOneMessage*) pvPortMalloc(messageSize);
  MONMessage->messageSize = messageSize - sizeof(DataMessageGeneric);
  MONMessage->RTcount = MONCOUNT_MONONEMESSAGE;
  MONMessage->uptime = millis();
  MONMessage->TxQ = LoraMesher::getInstance().getSendQueueSize();
  MONMessage->RxQ = LoraMesher::getInstance().getReceivedQueueSize();
  MONMessage->number_of_neighbors = number_of_neighbors;
  MONMessage->appPortDst = appPort::MQTTApp;
  MONMessage->appPortSrc = appPort::MonApp;
  MONMessage->addrSrc = LoraMesher::getInstance().getLocalAddress();
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
    }
    else {
      uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
      ESP_LOGD(MON_TAG, "Stack space unused after entering the task: %d",
        uxHighWaterMark);
      RoutingTableService::printRoutingTable();
      LoRaMeshService::getInstance().updateRoutingTable();
      LM_LinkedList<RouteNode>* routingTableList = LoRaMeshService::getInstance().routingTableList;
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
          monOneMessage* MONMessage = getInstance().createMONPayloadMessage(monMessagecount);
          int i = 0;
          do {
            RouteNode* rtn = routingTableList->getCurrent();
            if (rtn->networkNode.address == rtn->via) {
              MONMessage->rt[i++] = {
                rtn->networkNode.address,
                rtn->receivedSNR,
                rtn->SRTT
              };
            }
          } while (routingTableList->next());
          ESP_LOGV(MON_TAG, "sending monOneMessage");
          // Send the message
          MessageManager::getInstance().sendMessage(messagePort::MqttPort,
            (DataMessage*) MONMessage);
          // Delete the message
          vPortFree(MONMessage);
        }
        else {
          ESP_LOGD(MON_TAG, "sendingLoopOneMessage: no neighbors?");
        }
      }
      else {
        ESP_LOGD(MON_TAG, "No routes");
      }
      // end send MON
      vTaskDelay(MON_SENDING_EVERY / portTICK_PERIOD_MS);
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
    }
    else {
      uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
      ESP_LOGD(MON_TAG, "Stack space unused after entering the task: %d",
        uxHighWaterMark);
      // send MON, one entri per mqtt message
      // from RoutingTableService::printRoutingTable
      // LM_LinkedList<RouteNode> *routingTableList =
      // LoRaMeshService::getInstance().get_routingTableList();
      RoutingTableService::printRoutingTable();
      LM_LinkedList<RouteNode>* routingTableList = LoRaMeshService::getInstance().radio.routingTableListCopy();
      routingTableList->setInUse();
      if (routingTableList->moveToStart()) {
        MonService::getInstance().monMessageId++;
        uint16_t monMessagecount = 0;
        do {
          RouteNode* rtn = routingTableList->getCurrent();
          getInstance().createAndSendMessage(++monMessagecount, rtn);
        } while (routingTableList->next());
      }
      else {
        ESP_LOGD(MON_TAG, "No routes");
      }
      //Release routing table list usage.
      routingTableList->releaseInUse();
      routingTableList->Clear();
      // end send MON
      vTaskDelay(MON_SENDING_EVERY / portTICK_PERIOD_MS);
      // Print the free heap memory
      ESP_LOGD(MON_TAG, "Free heap: %d", esp_get_free_heap_size());
    }
  }
}

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
  MessageManager::getInstance().sendMessage(messagePort::MqttPort,
    (DataMessage*) message);
  // Delete the message
  delete message;
}

#endif
