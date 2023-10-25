#pragma once

#include <Arduino.h>

#include "message/messageService.h"
#include "message/messageManager.h"

#include "sensorCommandService.h"
#include "sensorServiceMessage.h"

#include "config.h"

#include "LoraMesher.h"


// Sensor types
#include "types/PHSensor/PHSensor.h"
#include "types/SHT4x/SHT4xAirSensor.h"
#include "types/SoilSensor/SoilHTSensor.h"
#include "types/WaterLevel/WaterLevelSensor.h"

// Metadata
#include "sensor/metadata/metadata.h"

// Services
#include "gps/gpsService.h"

class SensorService: public MessageService {
public:
    /**
     * @brief Construct a new GPSService object
     *
     */
    static SensorService& getInstance() {
        static SensorService instance;
        return instance;
    }

    void init();

    String getJSON(DataMessage* message);

    DataMessage* getDataMessage(JsonObject data);

    void processReceivedMessage(messagePort port, DataMessage* message);

    void sensorsOn();

    void sensorsOff();

    SensorCommandService* sensorCommandService = new SensorCommandService();

private:
    SensorService(): MessageService(SensorApp, "Sensor") {
        commandService = sensorCommandService;
    };

    PHSensor* phSensor = new PHSensor();
    SHT4xAirSensor* sht4xAirSensor = new SHT4xAirSensor();
    SoilHTSensor* soilSensor = new SoilHTSensor();
    WaterLevelSensor* waterLevelSensor = new WaterLevelSensor();

    void createSendingTask();

    static void sendingLoop(void*);

    TaskHandle_t sending_TaskHandle = NULL;

    bool running = false;

    bool isCreated = false;

    size_t sensorMessageId = 0;

    void createAndSendMessage();

    DataMessage* getMeasurementMessage(JsonObject data);

    DataMessage* getCalibrateMessage(JsonObject data);
};