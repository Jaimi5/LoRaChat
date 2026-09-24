#pragma once

#include <Arduino.h>

#include "wifi/wifiServerService.h"

#include "mqtt/mqttService.h"

#include "message/messageService.h"

#include "message/messageManager.h"

#include "simCommandService.h"

#include "simMessage.h"

#include "loramesh/loraMeshService.h"

#include "WiFi.h"

class Sim : public MessageService {
public:
    /**
     * @brief Construct a new GPSService object
     *
     */
    static Sim& getInstance() {
        static Sim instance;
        return instance;
    }

    ~Sim() {
        if (simCommandService != nullptr) {
            delete simCommandService;
        }
    }

    SimCommandService* simCommandService = nullptr;

#ifndef USE_LORAMESHER_V2
    SimulatorService* service = nullptr;
#endif

    void init();

    String start();

    String stop();

    String getJSON(DataMessage* message);

    DataMessage* getDataMessage(JsonObject data);

    void processReceivedMessage(messagePort port, DataMessage* message);

    void sendPacketsToServer(size_t packetCount, size_t packetSize, size_t delayMs);

private:
    Sim() : MessageService(SimApp, "Sim") {
        simCommandService = new SimCommandService();
        commandService = simCommandService;
    };

    TaskHandle_t sim_TaskHandle = NULL;

    void createSimTask();

    static void simLoop(void*);

    bool running = false;

    void sendAllData();

#ifndef USE_LORAMESHER_V2
    SimMessage* createSimMessage(LM_State* state);
#endif

    SimMessage* createSimPayloadMessage(size_t packetSize);

    SimMessage* createSimMessage(SimCommand command);

    void sendStartSimMessage();
};
