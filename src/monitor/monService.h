#pragma once
#include <Arduino.h>
#include <cstdint>
#include "config.h"

#ifdef USE_LORAMESHER_V2
#include "loramesher.hpp"
#else
#include "LoraMesher.h"
#endif

#include "message/messageManager.h"
#include "message/messageService.h"
#include "monCommandService.h"
#include "monServiceMessage.h"

#define MON_MQTT_ONE_MESSAGE

class MonService : public MessageService {
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
    MonService() : MessageService(MonApp, "Mon") { commandService = monCommandService_; };
    void createSendingTask();
#if defined(MON_MQTT_ONE_MESSAGE)
    static void sendingLoopOneMessage(void*);
    monOneMessage* createMONPayloadMessage(int number_of_neighbors);
#else
    static void sendingLoop(void*);
#ifdef USE_LORAMESHER_V2
    void createAndSendMessage(uint16_t mcount, const loramesher::RouteEntry& route);
#else
    void createAndSendMessage(uint16_t mcount, RouteNode*);
#endif
#endif
    TaskHandle_t sending_TaskHandle = NULL;
    bool running = false;
    bool isCreated = false;
    size_t monMessageId = 0;
};
