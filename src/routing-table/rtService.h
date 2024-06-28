#pragma once
#include <Arduino.h>
#include "message/messageService.h"
#include "message/messageManager.h"
#include "rtCommandService.h"
#include "rtServiceMessage.h"
#include "config.h"
#include "LoraMesher.h"

class RtService: public MessageService {
public:
    /**
     * @brief Construct a new GPSService object
     *
     */
    static RtService& getInstance() {
        static RtService instance;
        return instance;
    }
    void init();
    rtCommandService* rtCommandService_ = new rtCommandService();
    String getJSON(DataMessage* message);
    DataMessage* getDataMessage(JsonObject data);
    void processReceivedMessage(messagePort port, DataMessage* message);
private:
    RtService(): MessageService(RtApp, "Rt") {
        commandService = rtCommandService_;
    };
    void createSendingTask();
    static void sendingLoop(void*);
    TaskHandle_t sending_TaskHandle = NULL;
    bool running = false;
    bool isCreated = false;
    size_t rtMessageId = 0;
    void createAndSendMessage(uint16_t mcount, RouteNode*);
};
