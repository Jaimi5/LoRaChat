#pragma once

#include <Arduino.h>

#include <OneWire.h>

#include <DallasTemperature.h>

#include <ArduinoLog.h>

#include "sensor/sensor.h"

#include "temperatureCommandService.h"

#include "temperatureMessage.h"

class Temperature: public Sensor<float> {
public:
    /**
     * @brief Construct a new GPSService object
     *
     */
    static Temperature& getInstance() {
        static Temperature instance;
        return instance;
    }

    void init() override;

    void start() override;

    void pause() override;

    float readValue() override;

    float readValueWait(uint8_t retries) override;

    TemperatureCommandService* temperatureCommandService = new TemperatureCommandService();

    String getJSON(DataMessage* message);

private:
    Temperature(): Sensor(TemperatureSensorApp, "Temperature", TEMP_UPDATE_DELAY) {
        commandService = temperatureCommandService;
    };

    OneWire oneWire;
    DallasTemperature sensors;

    TaskHandle_t temperature_TaskHandle = NULL;

    void createTemperatureTask();

    static void temperatureLoop(void*);

    void sendTemperature(float value);

    TemperatureMessage* getTemperatureForSend(float value);
};