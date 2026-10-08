#pragma once
#include <Arduino.h>
#include <cstdint>
#include <vector>
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
    static void sendingLoopOneMessage(void*);
    static int getOneMessageSize(int neighbors) {
        return sizeof(monOneMessage) + sizeof(routing_entry) * neighbors;
    };
    monOneMessage* createMONPayloadMessage(int number_of_neighbors);
#ifdef USE_LORAMESHER_V2
    /** @return the routes this node reports: valid direct neighbours, or all valid routes. */
    static std::vector<routing_entry> collectReportedRoutes();

    /** Sends @p entries to MQTT in as many messages as MAX_MSG_SIZE requires. */
    void sendRoutes(const std::vector<routing_entry>& entries);
#endif
    TaskHandle_t sending_TaskHandle = NULL;
    bool running = false;
    bool isCreated = false;
    size_t monMessageId = 0;
};
