#include "sim.h"

#include <cstring>

// HELLO_PACKETS_DELAY is defined in the v1 LoRaMesher library; provide a default for v2
#ifndef HELLO_PACKETS_DELAY
#define HELLO_PACKETS_DELAY 120
#endif

static const char* SIM_TAG = "Sim";

void Sim::init() {
#ifndef USE_LORAMESHER_V2
    service = new SimulatorService();
#endif
    createSimTask();
    start();
}

String Sim::start() {
#ifndef USE_LORAMESHER_V2
    if (LOG_MESHER == true) {
        if (service != nullptr) {
            service->startSimulation();
        }
        LoraMesher::getInstance().setSimulatorService(service);
    }
#endif
    return "Sim On";
}

String Sim::stop() {
#ifndef USE_LORAMESHER_V2
    if (LOG_MESHER == true) {
        if (service != nullptr) {
            service->stopSimulation();
        }
        LoraMesher::getInstance().removeSimulatorService();
    }
#endif
    return "Sim Off";
}

String Sim::getJSON(DataMessage* message) {
    SimMessage* simMessage = (SimMessage*)message;

    DynamicJsonDocument doc(2048);

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

    // Guardrail: log the radio config actually applied at init. Emitted once
    // here (covers SIM_PDR_COMPARE mode, where this task is one-shot) and again
    // each cycle of the loop below, so the live SF/power is visible inside the
    // measurement window even when boot-time logs are not captured.
    ESP_LOGI(SIM_TAG, "radio config: %s",
             LoRaMeshService::getInstance().getRadioInfo().c_str());

#if SIM_PDR_COMPARE
    // ── PDR-comparison testbed mode ──────────────────────────────────────────
    // Pure LoRa load generator: no WiFi/MQTT orchestration and no LM_State dump
    // (that machinery is one-shot and v1-only). Wait for the mesh to converge
    // and a gateway to appear, then send one fixed-size, fixed-rate burst. The
    // gateways' serial logs capture the APP_TX / APP_RX markers used for PDR.
    //
    // Designate the sink without depending on the WiFi AP: in PDR mode the WiFi-cred
    // nodes are the sinks (SSID != "nowifi"). Set the gateway role directly at boot so
    // the pure-LoRa experiment doesn't hinge on AP reachability. The sink sends no burst;
    // messageManager.cpp logs APP_RX for every packet that reaches it.
    if (strcmp(WIFI_SSID, "nowifi") != 0) {
        LoRaMeshService::getInstance().setGateway();
        ESP_LOGI(SIM_TAG, "Simulator (testbed PDR mode) acting as gateway/sink (no burst)");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(SIM_TAG, "Simulator (testbed PDR mode) waiting for mesh to converge");
    vTaskDelay(SIM_TESTBED_WARMUP_MS / portTICK_PERIOD_MS);
    while (!LoRaMeshService::getInstance().hasGateway()) {
        ESP_LOGI(SIM_TAG, "Simulator (testbed PDR mode) waiting for a gateway");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(SIM_TAG, "Simulator (testbed PDR mode) sending %d packets of %d B every %d ms",
             PACKET_COUNT, PACKET_SIZE, PACKET_DELAY);
    sim.sendPacketsToServer(PACKET_COUNT, PACKET_SIZE, PACKET_DELAY);
    ESP_LOGI(SIM_TAG, "Simulator (testbed PDR mode) finished burst");
    vTaskDelete(NULL);
    return;
#endif

    for (;;) {
        sim.sendStartSimMessage();

        vTaskDelay(HELLO_PACKETS_DELAY * SIM_NETWORK_PROPAGATION_MULTIPLIER * 1000 /
                   portTICK_PERIOD_MS);  // Wait to propagate all the network status

        ESP_LOGI(SIM_TAG, "Heap size start sim: %d", ESP.getFreeHeap());

        // Periodic radio-config guardrail (see note at simLoop entry).
        ESP_LOGI(SIM_TAG, "radio config: %s",
                 LoRaMeshService::getInstance().getRadioInfo().c_str());


#if ONE_SENDER != 0
        if (LoRaMeshService::getInstance().getLocalAddress() == ONE_SENDER) {
            while (!LoRaMeshService::getInstance().hasGateway()) {
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

        vTaskDelay(SIM_INITIAL_WIFI_DELAY /
                   portTICK_PERIOD_MS);  // Wait for WiFi connection to establish

        MqttService::getInstance().connect();

        int retries = 0;
        while (!MqttService::getInstance().isDeviceConnected() && retries < MAX_CONNECTION_TRY) {
            ESP_LOGW(SIM_TAG, "Waiting for MQTT connection before sending data, retry %d/%d",
                     retries + 1, MAX_CONNECTION_TRY);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            MqttService::getInstance().connect();
            retries++;
        }

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

#ifndef USE_LORAMESHER_V2
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
#endif

    ESP_LOGI(SIM_TAG, "Simulator Finished sending data");

    simMessage = createSimMessage(SimCommand::EndedSimulationStatus);

    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*)simMessage);

    delete simMessage;
}

#ifndef USE_LORAMESHER_V2
SimMessage* Sim::createSimMessage(LM_State* state) {
    uint32_t messageSize = sizeof(SimMessage) + sizeof(SimMessageState);

    SimMessage* simMessage = (SimMessage*)pvPortMalloc(messageSize);

    simMessage->messageSize = messageSize - sizeof(DataMessageGeneric);
    simMessage->simCommand = SimCommand::Message;

    memcpy(simMessage->payload, state, sizeof(SimMessageState));

    simMessage->appPortDst = appPort::MQTTApp;
    simMessage->appPortSrc = appPort::SimApp;
    simMessage->addrSrc = LoRaMeshService::getInstance().getLocalAddress();
    simMessage->addrDst = 0;
    simMessage->messageId = state->id;

    return simMessage;
}
#endif

void Sim::sendPacketsToServer(size_t packetCount, size_t packetSize, size_t delayMs) {
    SimMessage* simPayloadMessage = createSimPayloadMessage(packetSize);
    for (size_t i = 0; i < packetCount; i++) {
        simPayloadMessage->messageId = i;
        // Guardrail, logged with EVERY packet (not just i==0): on v2 SF7/SF9 the
        // monitor misses the first 1-2 packets on some nodes (006C/1484/2A9C/7984),
        // so an i==0-only line would be lost on exactly those nodes. Per-packet
        // guarantees the applied SF/power is captured as long as ANY packet is.
        ESP_LOGI(SIM_TAG, "radio config: %s",
                 LoRaMeshService::getInstance().getRadioInfo().c_str());
        // Version-neutral TX marker for end-to-end PDR (matched against APP_RX at
        // the sink, keyed by src+seq). INFO level so it survives the testbed log
        // filter. `size` is the LoRaMesher payload requested for this burst.
        ESP_LOGI(SIM_TAG, "APP_TX src=0x%04X seq=%u size=%u",
                 LoRaMeshService::getInstance().getLocalAddress(), (unsigned)i,
                 (unsigned)packetSize);
        ESP_LOGI(SIM_TAG, "Simulator sending packet %d", i);
        MessageManager::getInstance().sendMessage(messagePort::MqttPort,
                                                  (DataMessage*)simPayloadMessage);

#ifdef USE_LORAMESHER_V2
        // Static pacing: a fixed inter-packet delay so the offered load is exactly
        // `delayMs` and reproducible. Per-SF capacity matching is done by choosing
        // `delayMs` (the TDMA superframe stretches with SF, so a single SF-independent
        // rate saturates high SF — see docs/paper/sf_offered_load_capacity.md).
        vTaskDelay(delayMs / portTICK_PERIOD_MS);
#else
        vTaskDelay(delayMs / portTICK_PERIOD_MS);  // Wait delayMs milliseconds

        // Wait until the previous packet has been sent
        while (LoRaMeshService::getInstance().queueWaitingSendPacketsLength() > 3) {
            ESP_LOGI(SIM_TAG, "Simulator waiting for packet to be sent");
            vTaskDelay(
                SIM_QUEUE_CONGESTION_DELAY /
                portTICK_PERIOD_MS);  // Wait when queue is congested before sending next packet
        }
#endif

        ESP_LOGI(SIM_TAG, "FREE HEAP: %d", ESP.getFreeHeap());
    }

    vPortFree(simPayloadMessage);
}

SimMessage* Sim::createSimPayloadMessage(size_t packetSize) {
    // packetSize = desired LoRaMesher payload bytes.
    // App overhead inside that payload: LoRaMeshMessage(3) + SimCommand(1) + packetSize field(4) = 8 bytes.
    constexpr size_t kAppOverhead = sizeof(LoRaMeshMessage) + sizeof(SimCommand) + sizeof(uint32_t);
    const size_t dataSize = (packetSize > kAppOverhead) ? (packetSize - kAppOverhead) : 0;

    uint32_t messageSize = sizeof(SimMessage) + sizeof(SimPayloadMessage) + dataSize;

    SimMessage* simMessage = (SimMessage*)pvPortMalloc(messageSize);
    simMessage->messageSize = messageSize - sizeof(DataMessageGeneric);

    simMessage->simCommand = SimCommand::Payload;
    simMessage->appPortDst = appPort::MQTTApp;
    simMessage->appPortSrc = appPort::SimApp;
    simMessage->addrSrc = LoRaMeshService::getInstance().getLocalAddress();
    simMessage->addrDst = 0;
    simMessage->messageId = 0;
    SimPayloadMessage* simPayloadMessage = (SimPayloadMessage*)simMessage->payload;
    simPayloadMessage->packetSize = dataSize;

    // Add 0, 1, 2, 3... dataSize to the payload
    for (size_t i = 0; i < dataSize; i++) {
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
    simMessage->addrSrc = LoRaMeshService::getInstance().getLocalAddress();
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

    int retries = 0;
    while (!MqttService::getInstance().isDeviceConnected() && retries < MAX_CONNECTION_TRY) {
        ESP_LOGW(SIM_TAG, "Waiting for MQTT connection, retry %d/%d", retries + 1,
                 MAX_CONNECTION_TRY);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        MqttService::getInstance().connect();
        retries++;
    }

    if (!MqttService::getInstance().isDeviceConnected()) {
        ESP_LOGE(SIM_TAG, "Failed to connect to MQTT after %d retries, skipping start message",
                 MAX_CONNECTION_TRY);
        return;
    }

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
    if (LoRaMeshService::getInstance().getLocalAddress() == WIFI_ADDR_CONNECTED)
        return;

    MqttService::getInstance().disconnect();
    WiFiServerService::getInstance().disconnectWiFi();
    WiFiServerService::getInstance().resetWiFiData();
}
