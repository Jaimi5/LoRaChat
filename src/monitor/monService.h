#pragma once
#include "LoraMesher.h"
#include "config.h"
#include "message/messageManager.h"
#include "message/messageService.h"
#include "monCommandService.h"
#include "monServiceMessage.h"
#include <Arduino.h>
#include <cstdint>

#define MON_MQTT_ONE_MESSAGE

class MonService: public MessageService {
public:
  /**
   * @brief Construct a new GPSService object
   *
   */
  static MonService& getInstance() {
    static MonService instance;
    return instance;
  }
  void init();
  monCommandService* monCommandService_ = new monCommandService();
  String getJSON(DataMessage* message);
  DataMessage* getDataMessage(JsonObject data);
  void processReceivedMessage(messagePort port, DataMessage* message);
private:
  MonService(): MessageService(MonApp, "Mon") {
    commandService = monCommandService_;
  };
  void createSendingTask();
#if defined(MON_MQTT_ONE_MESSAGE)
  static void sendingLoopOneMessage(void*);
  monOneMessage* createMONPayloadMessage(int number_of_neighbors);
#else
  static void sendingLoop(void*);
  void createAndSendMessage(uint16_t mcount, RouteNode*);
#endif
  TaskHandle_t sending_TaskHandle = NULL;
  bool running = false;
  bool isCreated = false;
  size_t monMessageId = 0;
};
