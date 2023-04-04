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
        vTaskDelay(60000 * 15 / portTICK_PERIOD_MS); // Wait 15 minutes

        sim.stop();

        Temperature::getInstance().pause();

        vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait 10 seconds to avoid other messages to propagate

        sim.sendAllData();
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
            delete simMessage;

            vTaskDelay(40000 / portTICK_PERIOD_MS); // Wait 40 seconds (to avoid flooding the network
        } while (service->statesList->getLength() > 0);
    }
}

SimMessage* Sim::createSimMessage(LM_State* state) {
    SimMessage* simMessage = new SimMessage();

    simMessage->messageSize = sizeof(SimMessage) - sizeof(DataMessageGeneric);
    simMessage->simCommand = SimCommand::Message;
    simMessage->state = *state;

    simMessage->appPortDst = appPort::MQTTApp;
    simMessage->appPortSrc = appPort::SimApp;
    simMessage->addrSrc = LoraMesher::getInstance().getLocalAddress();
    simMessage->addrDst = 0;
    simMessage->messageId = state->id;

    return simMessage;
}
