#include "led.h"

static const char* LED_TAG = "LedService";


void Led::init() {
    pinMode(LED, OUTPUT);
}

String Led::ledOn() {
    digitalWrite(LED, LED_ON);
    ESP_LOGV(LED_TAG, "Led On");
    state = 1;
    return "Led On";
}

String Led::ledOn(uint16_t dst) {
    ESP_LOGV(LED_TAG, "Led On to %X", dst);
    if (dst == LoraMesher::getInstance().getLocalAddress())
        return ledOn();

    DataMessage* msg = getLedMessage(LedCommand::On, dst);
    MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg);

    delete msg;

    return "Led On";
}

String Led::ledOff() {
    digitalWrite(LED, LED_OFF);
    ESP_LOGV(LED_TAG, "Led Off");
    state = 0;
    return "Led Off";
}

String Led::ledOff(uint16_t dst) {
    ESP_LOGV(LED_TAG, "Led Off to %X", dst);
    if (dst == LoraMesher::getInstance().getLocalAddress())
        return ledOff();

    DataMessage* msg = getLedMessage(LedCommand::Off, dst);
    MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, msg);

    delete msg;

    return "Led Off";
}

String Led::ledBlink() {
    if (state == 1) {
        ledOff();
        delay(200);
        ledOn();
        delay(200);
        ledOff();
        delay(200);
        ledOn();
    }
    else {
        ledOn();
        delay(200);
        ledOff();
        delay(200);
        ledOn();
        delay(200);
        ledOff();
    }
    return "Led Blink";
}

String Led::getJSON(DataMessage* message) {
    LedMessage* ledMessage = (LedMessage*) message;

    StaticJsonDocument<200> doc;

    JsonObject data = doc.createNestedObject("data");

    ledMessage->serialize(data);

    String json;
    serializeJson(doc, json);

    return json;
}

DataMessage* Led::getDataMessage(JsonObject data) {
    LedMessage* ledMessage = new LedMessage();

    ledMessage->deserialize(data);

    ledMessage->messageSize = sizeof(LedMessage) - sizeof(DataMessageGeneric);

    return ((DataMessage*) ledMessage);
}

DataMessage* Led::getLedMessage(LedCommand command, uint16_t dst) {
    LedMessage* ledMessage = new LedMessage();

    ledMessage->messageSize = sizeof(LedMessage) - sizeof(DataMessageGeneric);

    ledMessage->ledCommand = command;

    ledMessage->appPortSrc = appPort::LedApp;
    ledMessage->appPortDst = appPort::LedApp;

    ledMessage->addrSrc = LoraMesher::getInstance().getLocalAddress();
    ledMessage->addrDst = dst;

    return (DataMessage*) ledMessage;
}

void Led::processReceivedMessage(messagePort port, DataMessage* message) {
    LedMessage* ledMessage = (LedMessage*) message;

    switch (ledMessage->ledCommand) {
        case LedCommand::On:
            ledOn();
            break;
        case LedCommand::Off:
            ledOff();
            break;
        default:
            break;
    }
}
