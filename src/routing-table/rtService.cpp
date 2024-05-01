#include "rtService.h"
#include "loramesh/loraMeshService.h"
#include "LoraMesher.h"

static const char* RT_TAG = "RtService";

void RtService::init() {
  ESP_LOGI(RT_TAG, "Initializing mqtt_rt");
  createSendingTask();
}

String RtService::getJSON(DataMessage* message) {
  rtMessage* rt = (rtMessage*) message;
  StaticJsonDocument<2000> doc;
  // JsonObject root = doc.to<JsonObject>();
  // rt->serialize(root);
  JsonObject data = doc.createNestedObject("RT");
  rt->serialize(data);
  String json;
  serializeJson(doc, json);
  return json;
}

DataMessage* RtService::getDataMessage(JsonObject data) {
  rtMessage* rt = new rtMessage();
  rt->deserialize(data);
  rt->messageSize = sizeof(rtMessage) - sizeof(DataMessageGeneric);
  return ((DataMessage*) rt);
}

void RtService::processReceivedMessage(messagePort port, DataMessage* message) {
  rtMessage* rt = (rtMessage*) message;
  ESP_LOGI(RT_TAG, "Received rt data");
}

void RtService::createSendingTask() {
  BaseType_t res =
    xTaskCreatePinnedToCore(sendingLoop, /* Function to implement the task */
      "SendingTask",       /* Name of the task */
      6000,                /* Stack size in words */
      NULL,                /* Task input parameter */
      1,                   /* Priority of the task */
      &sending_TaskHandle, /* Task handle. */
      0); /* Core where the task should run */

  if (res != pdPASS) {
    ESP_LOGE(RT_TAG, "Sending task creation failed");
    return;
  }
  ESP_LOGI(RT_TAG, "Sending task created");
  running = true;
  isCreated = true;
}

void RtService::sendingLoop(void* parameter) {
  RtService& rtService = RtService::getInstance();
  UBaseType_t uxHighWaterMark;

  ESP_LOGI(RT_TAG, "entering sendingLoop");
  while (true) {
    if (!rtService.running) {
      // Wait until a notification to start the task
      ESP_LOGI(RT_TAG, "Wait notification to start the task");
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      ESP_LOGI(RT_TAG, "received notification to start the task");
    }
    else {
      uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
      ESP_LOGD(RT_TAG, "Stack space unused after entering the task: %d",
        uxHighWaterMark);
      // send RT, one entry per mqtt message
      RoutingTableService::printRoutingTable();
      LoRaMeshService::getInstance().updateRoutingTable();
      LM_LinkedList<RouteNode>* routingTableList = LoRaMeshService::getInstance().routingTableList;

      routingTableList->setInUse();
      if (routingTableList->moveToStart()) {
        RtService::getInstance().rtMessageId++;
        uint16_t rtMessageCount = 0;
        do {
          RouteNode* rtn = routingTableList->getCurrent();
          getInstance().createAndSendMessage(++rtMessageCount, rtn);
        } while (routingTableList->next());
      }
      else {
        getInstance().createAndSendMessage(0, NULL);
        ESP_LOGD(RT_TAG, "No routes");
      }
      //Release routing table list usage.
      routingTableList->releaseInUse();
      routingTableList->Clear();
      // end send RT
      vTaskDelay(RT_SENDING_EVERY / portTICK_PERIOD_MS);
      // Print the free heap memory
      ESP_LOGD(RT_TAG, "Free heap: %d", esp_get_free_heap_size());
    }
  }
}

void RtService::createAndSendMessage(uint16_t mcount, RouteNode* rtn) {
  ESP_LOGV(RT_TAG, "Sending rt data %d", RtService::getInstance().rtMessageId);
  rtMessage* message = new rtMessage();
  message->appPortDst = appPort::MQTTApp;
  message->appPortSrc = appPort::RtApp;
  message->messageId = rtMessageId;
  message->addrSrc = LoraMesher::getInstance().getLocalAddress();
  message->addrDst = 0;
  //
  message->RTcount = mcount;
  if (rtn) {
    message->address = rtn->networkNode.address;
    message->metric = rtn->networkNode.metric;
    message->via = rtn->via;
    message->receivedSNR = rtn->receivedSNR;
    message->sentSNR = rtn->sentSNR;
    message->SRTT = rtn->SRTT;
    message->RTTVAR = rtn->RTTVAR;
  }

  ESP_LOGV(RT_TAG, "routing table");
  message->messageSize = sizeof(rtMessage) - sizeof(DataMessageGeneric);
  // Send the message
  MessageManager::getInstance().sendMessage(messagePort::MqttPort,
    (DataMessage*) message);
  // Delete the message
  delete message;
}
