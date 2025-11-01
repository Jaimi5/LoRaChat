#include "sim.h"

static const char* SIM_TAG = "Sim";

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
    SimMessage* simMessage = (SimMessage*)message;

    StaticJsonDocument<2048> doc;

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

    return ((DataMessage*)simMessage);
}

void Sim::processReceivedMessage(messagePort port, DataMessage* message) {
    SimMessage* simMessage = (SimMessage*)message;

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
    xTaskCreate(simLoop,          /* Task function. */
                "SimTask",        /* name of task. */
                8192,             /* Stack size of task */
                (void*)1,         /* parameter of the task */
                2,                /* priority of the task */
                &sim_TaskHandle); /* Task handle to keep track of created task */
}

void Sim::simLoop(void* pvParameters) {
    ESP_LOGI(SIM_TAG, "Simulator started");
    Sim sim = Sim::getInstance();

    for (;;) {
        sim.sendStartSimMessage();

        vTaskDelay(HELLO_PACKETS_DELAY * SIM_NETWORK_PROPAGATION_MULTIPLIER * 1000 /
                   portTICK_PERIOD_MS);  // Wait to propagate all the network status

        ESP_LOGI(SIM_TAG, "Heap size start sim: %d", ESP.getFreeHeap());


#if ONE_SENDER != 0
        if (LoraMesher::getInstance().getLocalAddress() == ONE_SENDER) {
            while (LoraMesher::getInstance().getClosestGateway() == nullptr) {
                vTaskDelay(1000 / portTICK_PERIOD_MS);  // Wait 1 second
            }
            sim.sendPacketsToServer(PACKET_COUNT, PACKET_SIZE, PACKET_DELAY);
        } else {
            vTaskDelay(SIM_NON_SENDER_WAIT /
                       portTICK_PERIOD_MS);  // Wait to avoid other messages to propagate
            vTaskDelay(PACKET_DELAY * PACKET_COUNT / portTICK_PERIOD_MS);
        }
#else
        sim.sendPacketsToServer(PACKET_COUNT, PACKET_SIZE, PACKET_DELAY);
#endif


        ESP_LOGI(SIM_TAG, "Simulator stopped");

        while (LoRaMeshService::getInstance().hasActiveConnections()) {
            ESP_LOGI(SIM_TAG, "Simulator waiting for connections to be closed");
            vTaskDelay(PACKET_DELAY * 1.5 /
                       portTICK_PERIOD_MS);  // Wait PACKET_DELAY * 1.5 milliseconds
        }

        sim.stop();

        ESP_LOGI(SIM_TAG, "Heap size finished sim: %d", ESP.getFreeHeap());

        // LoRaMeshService::getInstance().standby();

        ESP_LOGI(SIM_TAG, "Simulator connecting to WiFi");

        WiFiServerService::getInstance().addSSID(WIFI_SSID);
        WiFiServerService::getInstance().addPassword(WIFI_PASSWORD);

        WiFiServerService::getInstance().connectWiFi();

        MqttService::getInstance().connect();

        vTaskDelay(SIM_POST_MQTT_DELAY /
                   portTICK_PERIOD_MS);  // Wait for MQTT connection to stabilize

        sim.sendAllData();

        vTaskDelete(NULL);
    }
}

void Sim::sendAllData() {
    ESP_LOGI(SIM_TAG, "Simulator sending data");

    SimMessage* simMessage = createSimMessage(SimCommand::EndedSimulation);

    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*)simMessage);

    delete simMessage;

    service->statesList->setInUse();

    if (service->statesList->moveToStart()) {
        ESP_LOGI(SIM_TAG, "Simulator sending data, n. %d", service->statesList->getLength());
        do {
            LM_State* state = service->statesList->Pop();
            if (state == nullptr) {
                continue;
            }

            simMessage = createSimMessage(state);

            MessageManager::getInstance().sendMessage(messagePort::MqttPort,
                                                      (DataMessage*)simMessage);
            delete state;
            vPortFree(simMessage);

            // If wifi connected wait configured delay, else wait longer to avoid flooding
            if (WiFi.status() == WL_CONNECTED)
                vTaskDelay(SIM_UPLOAD_DELAY_CONNECTED /
                           portTICK_PERIOD_MS);  // Wait between uploads when connected
            else
                vTaskDelay(SIM_UPLOAD_DELAY_DISCONNECTED /
                           portTICK_PERIOD_MS);  // Wait longer when disconnected to avoid flooding

        } while (service->statesList->getLength() > 0);
    }

    service->statesList->releaseInUse();

    ESP_LOGI(SIM_TAG, "Simulator Finished sending data");

    simMessage = createSimMessage(SimCommand::EndedSimulationStatus);

    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*)simMessage);

    delete simMessage;
}

SimMessage* Sim::createSimMessage(LM_State* state) {
    uint32_t messageSize = sizeof(SimMessage) + sizeof(SimMessageState);

    SimMessage* simMessage = (SimMessage*)pvPortMalloc(messageSize);

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
        ESP_LOGI(SIM_TAG, "Simulator sending packet %d", i);
        MessageManager::getInstance().sendMessage(messagePort::MqttPort,
                                                  (DataMessage*)simPayloadMessage);

        vTaskDelay(delayMs / portTICK_PERIOD_MS);  // Wait delayMs milliseconds

        // Wait until the previous packet has been sent
        while (LoRaMeshService::getInstance().queueWaitingSendPacketsLength() > 3) {
            ESP_LOGI(SIM_TAG, "Simulator waiting for packet to be sent");
            vTaskDelay(
                SIM_QUEUE_CONGESTION_DELAY /
                portTICK_PERIOD_MS);  // Wait when queue is congested before sending next packet
        }

        ESP_LOGI(SIM_TAG, "FREE HEAP: %d", ESP.getFreeHeap());
    }

    vPortFree(simPayloadMessage);
}

SimMessage* Sim::createSimPayloadMessage(size_t packetSize) {
    uint32_t messageSize = sizeof(SimMessage) + sizeof(SimPayloadMessage) + packetSize;

    SimMessage* simMessage = (SimMessage*)pvPortMalloc(messageSize);
    simMessage->messageSize = messageSize - sizeof(DataMessageGeneric);

    simMessage->simCommand = SimCommand::Payload;
    simMessage->appPortDst = appPort::MQTTApp;
    simMessage->appPortSrc = appPort::SimApp;
    simMessage->addrSrc = LoraMesher::getInstance().getLocalAddress();
    simMessage->addrDst = 0;
    simMessage->messageId = 0;
    SimPayloadMessage* simPayloadMessage = (SimPayloadMessage*)simMessage->payload;
    simPayloadMessage->packetSize = packetSize;

    // Add 0, 1, 2, 3... packetSize to the payload
    for (size_t i = 0; i < packetSize; i++) {
        simPayloadMessage->payload[i] = i;
        if (i % 100 == 0) {
            vTaskDelay(1 / portTICK_PERIOD_MS);  // Wait 1 milliseconds
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

    vTaskDelay(SIM_INITIAL_WIFI_DELAY /
               portTICK_PERIOD_MS);  // Wait for WiFi connection to establish

    MqttService::getInstance().connect();

    ESP_LOGI(SIM_TAG, "Simulator MQTT connected");

    ESP_LOGI(SIM_TAG, "Simulator sending start message");

    SimMessage* simMessage = createSimMessage(SimCommand::StartingSimulation);

    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*)simMessage);

    delete simMessage;

    vTaskDelay(SIM_POST_START_DELAY / portTICK_PERIOD_MS);  // Wait after sending start message

#if WIFI_ADDR_CONNECTED == 0
    return;
#endif

    // Delete WiFi and MQTT
    if (LoraMesher::getInstance().getLocalAddress() == WIFI_ADDR_CONNECTED)
        return;

    MqttService::getInstance().disconnect();
    WiFiServerService::getInstance().disconnectWiFi();
    WiFiServerService::getInstance().resetWiFiData();
}
