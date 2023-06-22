#include "sim.h"

void Sim::init() {
    service = new SimulatorService();
    createSimTask();
    start();
}

String Sim::start() {
    if (LOG_MESHER == true) {
        if (service != nullptr) {
            service->startSimulation();
        }
        LoraMesher::getInstance().setSimulatorService(service);
    }
    return "Sim On";
}

String Sim::stop() {
    if (LOG_MESHER == true) {
        if (service != nullptr) {
            service->stopSimulation();
        }
        LoraMesher::getInstance().removeSimulatorService();
    }
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
        8192, /* Stack size of task */
        (void*) 1, /* parameter of the task */
        2, /* priority of the task */
        &sim_TaskHandle); /* Task handle to keep track of created task */
}

void Sim::simLoop(void* pvParameters) {
    Log.verboseln(F("Simulator started"));
    Sim sim = Sim::getInstance();

    for (;;) {
        sim.sendStartSimMessage();

        vTaskDelay(60000 * 4 / portTICK_PERIOD_MS); // Wait 4 minutes to propagate all the network status

#ifdef ONE_SENDER
        if (LoraMesher::getInstance().getLocalAddress() == ONE_SENDER) {

            while (LoraMesher::getInstance().getClosestGateway() == nullptr) {
                vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait 1 second
            }
            sim.sendPacketsToServer(PACKET_COUNT, PACKET_SIZE, PACKET_DELAY);
        }
        else {
            vTaskDelay(60000 * 10 / portTICK_PERIOD_MS); // Wait 10 minutes to avoid other messages to propagate
            vTaskDelay(PACKET_DELAY * PACKET_COUNT / portTICK_PERIOD_MS);
        }
#else
        sim.sendPacketsToServer(PACKET_COUNT, PACKET_SIZE, PACKET_DELAY);
#endif


        Log.verboseln(F("Simulator stopped"));

        while (LoRaMeshService::getInstance().hasActiveConnections()) {
            vTaskDelay(PACKET_DELAY + 20000 / portTICK_PERIOD_MS); // Wait 10 second
        }

        sim.stop();

        // LoRaMeshService::getInstance().standby();

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


        sim.sendAllData();

        vTaskDelete(NULL);
    }
}

void Sim::sendAllData() {

    Log.verboseln(F("Simulator sending data"));

    SimMessage* simMessage = createSimMessage(SimCommand::EndedSimulation);

    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*) simMessage);

    delete simMessage;

    service->statesList->setInUse();

    if (service->statesList->moveToStart()) {
        Log.verboseln(F("Simulator sending data, n. %d"), service->statesList->getLength());
        do {
            LM_State* state = service->statesList->Pop();
            if (state == nullptr) {
                continue;
            }

            simMessage = createSimMessage(state);

            MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*) simMessage);
            delete state;
            free(simMessage);

            // If wifi connected wait 1 second, else wait 40 seconds
            if (WiFi.status() == WL_CONNECTED)
                vTaskDelay(2000 / portTICK_PERIOD_MS); // Wait 10 milliseconds
            else
                vTaskDelay(40000 / portTICK_PERIOD_MS); // Wait 40 seconds (to avoid flooding the network

        } while (service->statesList->getLength() > 0);
    }

    service->statesList->releaseInUse();

    Log.verboseln(F("Simulator Finished sending data"));

    simMessage = createSimMessage(SimCommand::EndedSimulationStatus);

    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*) simMessage);

    delete simMessage;
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
        Log.verboseln(F("Simulator sending packet %d"), i);
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
        if (i % 100 == 0) {
            vTaskDelay(1 / portTICK_PERIOD_MS); // Wait 1 milliseconds
        }
    }

    return simMessage;
}

SimMessage* Sim::createSimMessage(SimCommand command) {
    SimMessage* simMessage = new SimMessage();

    simMessage->messageSize = sizeof(SimMessage) - sizeof(DataMessageGeneric);
    simMessage->simCommand = command;

    simMessage->appPortDst = appPort::MQTTApp;
    simMessage->appPortSrc = appPort::SimApp;
    simMessage->addrSrc = LoraMesher::getInstance().getLocalAddress();
    simMessage->addrDst = 0;
    simMessage->messageId = 0;

    return simMessage;
}

void Sim::sendStartSimMessage() {
    WiFiServerService::getInstance().addSSID(WIFI_SSID);
    WiFiServerService::getInstance().addPassword(WIFI_PASSWORD);

    WiFiServerService::getInstance().connectWiFi();

    MqttService::getInstance().initMqtt(String(LoraMesher::getInstance().getLocalAddress()));

    vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait 1 second

    Log.verboseln(F("Simulator sending start message"));

    SimMessage* simMessage = createSimMessage(SimCommand::StartingSimulation);

    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*) simMessage);

    delete simMessage;

    vTaskDelay(30000 / portTICK_PERIOD_MS); // Wait 1 second

    // Delete WiFi and MQTT
    if (LoraMesher::getInstance().getLocalAddress() == WIFI_ADDR_CONNECTED)
        return;

    MqttService::getInstance().disconnect();
    WiFiServerService::getInstance().disconnectWiFi();
    WiFiServerService::getInstance().resetWiFiData();
}

