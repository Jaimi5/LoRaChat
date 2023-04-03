#include "temperature.h"

void Temperature::init() {
    oneWire = OneWire(ONE_WIRE_BUS);
    sensors = DallasTemperature(&oneWire);

    sensors.begin();

    createTemperatureTask();

    Log.noticeln(F("Temperature initialized"));

    start();
}

void Temperature::start() {
    running = true;
    xTaskNotifyGive(temperature_TaskHandle);

    Log.noticeln(F("Temperature task started"));
}

void Temperature::pause() {
    running = false;

    Log.noticeln(F("Temperature task paused"));
}

float Temperature::readValue() {
    return readValueWait(0);
}

float Temperature::readValueWait(uint8_t retries) {
    sensors.requestTemperatures();
    float temperature = sensors.getTempCByIndex(0);

    if (temperature == DEVICE_DISCONNECTED_C) {
        if (retries > 0) {
            return readValueWait(retries - 1);
        }
        else {
            return DEVICE_DISCONNECTED_C;
        }
    }

    return temperature;
}

String Temperature::getJSON(DataMessage* message) {
    TemperatureMessage* temperatureMessage = (TemperatureMessage*) message;

    DynamicJsonDocument doc(1024);

    JsonObject data = doc.createNestedObject("data");

    temperatureMessage->serialize(data);

    String json;
    serializeJson(doc, json);

    return json;
}

void Temperature::createTemperatureTask() {
    int res = xTaskCreate(
        temperatureLoop,
        "Temperature Task",
        4096,
        (void*) 1,
        2,
        &temperature_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Temperature task handle error: %d"), res);
    }
}

void Temperature::temperatureLoop(void*) {
    Temperature& temperature = Temperature::getInstance();

    while (true) {
        if (!temperature.running)
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        else {

            float value = temperature.readValue();

            if (value != DEVICE_DISCONNECTED_C) {
                Log.noticeln(F("Temperature: %f"), value);
            }
            else {
                Log.errorln(F("Temperature reading error"));
            }

            temperature.sendTemperature(value);

            vTaskDelay(temperature.readEveryMs / portTICK_PERIOD_MS);
        }
    }
}


void Temperature::sendTemperature(float value) {
    TemperatureMessage* message = getTemperatureForSend(value);
    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*) message);
    delete message;
}

TemperatureMessage* Temperature::getTemperatureForSend(float value) {

    TemperatureMessage* temperatureMessage = new TemperatureMessage(value);

    temperatureMessage->appPortDst = appPort::MQTTApp;
    temperatureMessage->appPortSrc = appPort::TemperatureSensorApp;
    temperatureMessage->addrSrc = LoRaMeshService::getInstance().getLocalAddress();
    temperatureMessage->addrDst = 0;
    temperatureMessage->messageId = sensorMessageId++;

    temperatureMessage->messageSize = sizeof(TemperatureMessage) - sizeof(DataMessageGeneric);

    return temperatureMessage;
}
