#include "sim.h"

void Sim::init() {
    service = new SimulatorService();
    createSimTask();
    start();
}

String Sim::start() {
    if (service != nullptr) {
        service->startSimulation();
    }
    LoraMesher::getInstance().setSimulatorService(service);
    return "Sim On";
}

String Sim::stop() {
    if (service != nullptr) {
        service->stopSimulation();
    }
    LoraMesher::getInstance().removeSimulatorService();

    return "Sim Off";
}

String Sim::getJSON(DataMessage* message) {
    SimMessage* simMessage = (SimMessage*) message;

    DynamicJsonDocument doc(1024);

    JsonObject data = doc.createNestedObject("data");

    simMessage->serialize(data);

    String json;
    serializeJson(doc, json);

    return json;
}

DataMessage* Sim::getDataMessage(JsonObject data) {
    SimMessage* simMessage = new SimMessage();

    simMessage->deserialize(data);

    simMessage->messageSize = sizeof(SimMessage) - sizeof(DataMessageGeneric);

    return ((DataMessage*) simMessage);
}

void Sim::processReceivedMessage(messagePort port, DataMessage* message) {
    SimMessage* simMessage = (SimMessage*) message;

    switch (simMessage->simCommand) {
        case SimCommand::StartSim:
            start();
            break;
        case SimCommand::StopSim:
            stop();
            break;
        default:
            break;
    }
}

void Sim::createSimTask() {
    xTaskCreate(
        simLoop, /* Task function. */
        "SimTask", /* name of task. */
        2048, /* Stack size of task */
        (void*) 1, /* parameter of the task */
        2, /* priority of the task */
        &sim_TaskHandle); /* Task handle to keep track of created task */
}

void Sim::simLoop(void* pvParameters) {
    Log.verboseln(F("Simulator started"));
    Sim sim = Sim::getInstance();

    for (;;) {
        vTaskDelay(60000 * 8 / portTICK_PERIOD_MS); // Wait 2 minutes to propagate all the network status

        sim.sendPacketsToServer(10, 10, 10000);

        Log.verboseln(F("Simulator stopped"));

        vTaskDelay(60000 * 5 / portTICK_PERIOD_MS); // Wait 5 minutes to avoid other messages to propagate

        sim.stop();

        Log.verboseln(F("Simulator connecting to WiFi"));

        WiFiServerService::getInstance().addSSID(WIFI_SSID);
        WiFiServerService::getInstance().addPassword(WIFI_PASSWORD);

        WiFiServerService::getInstance().connectWiFi();

        int maxTries = 10;
        while (WiFi.status() != WL_CONNECTED && maxTries > 0) {
            Log.verboseln(F("Simulator WiFi not connected"));
            vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait 1 second
            maxTries--;
            WiFiServerService::getInstance().connectWiFi();
        }

        MqttService::getInstance().initMqtt(String(LoraMesher::getInstance().getLocalAddress()));

        vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait 5 minutes to propagate through the network

        Log.verboseln(F("Simulator sending data"));

        sim.sendAllData();

        vTaskDelete(NULL);
    }
}

void Sim::sendAllData() {

    if (service->statesList->moveToStart()) {
        do {
            LM_State* state = service->statesList->Pop();
            if (state == nullptr) {
                continue;
            }

            SimMessage* simMessage = createSimMessage(state);

            MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*) simMessage);
            delete state;
            free(simMessage);

            // If wifi connected wait 1 second, else wait 40 seconds
            if (WiFi.status() == WL_CONNECTED)
                vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait 1 second
            else
                vTaskDelay(40000 / portTICK_PERIOD_MS); // Wait 40 seconds (to avoid flooding the network

        } while (service->statesList->getLength() > 0);
    }
}

SimMessage* Sim::createSimMessage(LM_State* state) {
    uint32_t messageSize = sizeof(SimMessage) + sizeof(SimMessageState);

    SimMessage* simMessage = (SimMessage*) malloc(messageSize);

    simMessage->messageSize = messageSize - sizeof(DataMessageGeneric);
    simMessage->simCommand = SimCommand::Message;

    memcpy(simMessage->payload, state, sizeof(SimMessageState));

    simMessage->appPortDst = appPort::MQTTApp;
    simMessage->appPortSrc = appPort::SimApp;
    simMessage->addrSrc = LoraMesher::getInstance().getLocalAddress();
    simMessage->addrDst = 0;
    simMessage->messageId = state->id;

    return simMessage;
}

void Sim::sendPacketsToServer(size_t packetCount, size_t packetSize, size_t delayMs) {
    SimMessage* simPayloadMessage = createSimPayloadMessage(packetSize);
    for (size_t i = 0; i < packetCount; i++) {
        simPayloadMessage->messageId = i;
        MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*) simPayloadMessage);

        vTaskDelay(delayMs / portTICK_PERIOD_MS); // Wait delayMs milliseconds
    }

    free(simPayloadMessage);
}

SimMessage* Sim::createSimPayloadMessage(size_t packetSize) {
    uint32_t messageSize = sizeof(SimMessage) + sizeof(SimPayloadMessage) + packetSize;

    SimMessage* simMessage = (SimMessage*) malloc(messageSize);
    simMessage->messageSize = messageSize - sizeof(DataMessageGeneric);

    simMessage->simCommand = SimCommand::Payload;
    simMessage->appPortDst = appPort::MQTTApp;
    simMessage->appPortSrc = appPort::SimApp;
    simMessage->addrSrc = LoraMesher::getInstance().getLocalAddress();
    simMessage->addrDst = 0;
    simMessage->messageId = 0;
    SimPayloadMessage* simPayloadMessage = (SimPayloadMessage*) simMessage->payload;
    simPayloadMessage->packetSize = packetSize;

    // Add 0, 1, 2, 3... packetSize to the payload
    for (size_t i = 0; i < packetSize; i++) {
        simPayloadMessage->payload[i] = i;
    }

    return simMessage;
}

