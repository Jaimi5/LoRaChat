#include "dht22.h"

void Dht22::init() {
    oneWire = OneWire(ONE_WIRE_BUS);
    sensors = DallasTemperature(&oneWire);

    sensors.begin();

    createDht22Task();

    Log.noticeln(F("Dht22 initialized"));

    start();
}

void Dht22::start() {
    running = true;
    xTaskNotifyGive(dht22_TaskHandle);

    Log.noticeln(F("Dht22 task started"));
}

void Dht22::pause() {
    running = false;

    Log.noticeln(F("Dht22 task paused"));
}

float Dht22::readValue() {
    return readValueWait(0);
}

float Dht22::readValueWait(uint8_t retries) {
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

String Dht22::getJSON(DataMessage* message) {
    Dht22Message* temperatureMessage = (Dht22Message*)message;

    DynamicJsonDocument doc(1024);

    JsonObject data = doc.createNestedObject("data");

    temperatureMessage->serialize(data);

    String json;
    serializeJson(doc, json);

    return json;
}

void Dht22::createDht22Task() {
    int res = xTaskCreate(
        dht22Loop,
        "Dht22 Task",
        4096,
        (void*)1,
        2,
        &dht22_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Dht22 task handle error: %d"), res);
    }
}

void Dht22::dht22Loop(void*) {
    Dht22& temperature = Dht22::getInstance();

    while (true) {
        if (!temperature.running)
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        else {

            float temperatureReading = temperature.readValue();
            float humidityReading = temperature.readValue();
            float pressionReading = temperature.readValue();

            if (temperatureReading != DEVICE_DISCONNECTED_C) {
                Log.noticeln(F("Dht22: %f"), temperatureReading);
            }
            else {
                Log.errorln(F("Dht22 reading error"));
            }

            temperature.sendDht22Readings(temperatureReading, humidityReading, pressionReading);

            vTaskDelay(temperature.readEveryMs / portTICK_PERIOD_MS);
        }
    }
}


void Dht22::sendDht22Readings(float temperature, float humidity, float pression) {
    Dht22Message* message = getDht22ReadingsForSend(temperature, humidity, pression);
    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*)message);
    delete message;
}

Dht22Message* Dht22::getDht22ReadingsForSend(float temperature, float humidity, float pression) {

    Dht22Message* temperatureMessage = new Dht22Message(temperature, humidity, pression);

    temperatureMessage->appPortDst = appPort::MQTTApp;
    temperatureMessage->appPortSrc = appPort::Dht22SensorApp;
    temperatureMessage->addrSrc = LoRaMeshService::getInstance().getLocalAddress();
    temperatureMessage->addrDst = 0;
    temperatureMessage->messageId = sensorMessageId++;

    temperatureMessage->messageSize = sizeof(Dht22Message) - sizeof(DataMessageGeneric);

    return temperatureMessage;
}
