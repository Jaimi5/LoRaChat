#pragma once

#include <Arduino.h>

#include <OneWire.h>

#include <DallasTemperature.h>

#include <ArduinoLog.h>

#include "sensor/sensor.h"

#include "dht22CommandService.h"

#include "dht22Message.h"

class Dht22: public Sensor<float> {
public:
    /**
     * @brief Construct a new Dht22 Sensor object
     *
     */
    static Dht22& getInstance() {
        static Dht22 instance;
        return instance;
    }

    void init() override;

    void start() override;

    void pause() override;

    float readValue() override;

    float readValueWait(uint8_t retries) override;

    Dht22CommandService* dht22CommandService = new Dht22CommandService();

    String getJSON(DataMessage* message);

private:
    Dht22(): Sensor(Dht22SensorApp, "Dht22", DHT22_UPDATE_DELAY) {
        commandService = dht22CommandService;
    };

    OneWire oneWire;
    DallasTemperature sensors;

    TaskHandle_t dht22_TaskHandle = NULL;

    void createDht22Task();

    static void dht22Loop(void*);

    void sendDht22Readings(float temperature, float humidity, float pression);

    Dht22Message* getDht22ReadingsForSend(float temperature, float humidity, float pression);
};