#include "sensorService.h"

static const char* SENSOR_TAG = "SensorService";


void SensorService::init() {
#if !defined(NO_SENSOR_DATA)
    ESP_LOGI(SENSOR_TAG, "Initializing sensors");

    phSensor->init();
    sht4xAirSensor->init();
    soilSensor->init();
    waterLevelSensor->init();

    ESP_LOGI(SENSOR_TAG, "Sensors initialized");

    createSendingTask();

    sensorsOn();
#endif
}

String SensorService::getJSON(DataMessage* message) {
    SensorCommandMessage* sensorMessage = (SensorCommandMessage*) message;

    StaticJsonDocument<2000> doc;

    JsonObject root = doc.to<JsonObject>();

    sensorMessage->serialize(root);

    String json;
    serializeJson(doc, json);

    return json;
}

DataMessage* SensorService::getDataMessage(JsonObject data) {
    switch ((SensorCommand) data["sensorCommand"]) {
        case SensorCommand::Data:
            return getMeasurementMessage(data);;
            break;

        case SensorCommand::Calibrate:
            return getCalibrateMessage(data);
            break;
    }

    ESP_LOGE(SENSOR_TAG, "Unknown sensor command: %d", data["sensorCommand"].as<uint8_t>());

    return nullptr;
}

DataMessage* SensorService::getMeasurementMessage(JsonObject data) {
    MeasurementMessage* measurement = new MeasurementMessage();
    measurement->deserialize(data);
    measurement->messageSize = sizeof(MeasurementMessage) - sizeof(DataMessageGeneric);
    return ((DataMessage*) measurement);
}

DataMessage* SensorService::getCalibrateMessage(JsonObject data) {
    MeasurementMessage* measurement = new MeasurementMessage();
    measurement->deserialize(data);
    measurement->messageSize = sizeof(MeasurementMessage) - sizeof(DataMessageGeneric);
    return ((DataMessage*) measurement);
}


void SensorService::processReceivedMessage(messagePort port, DataMessage* message) {
    SensorCommandMessage* sensorMessage = (SensorCommandMessage*) message;

    switch (sensorMessage->sensorCommand) {
        case SensorCommand::Data:
            ESP_LOGI(SENSOR_TAG, "Received sensor data");
            break;
        case SensorCommand::Calibrate:
            ESP_LOGI(SENSOR_TAG, "Received sensor calibrate command");
            break;
        default:
            break;
    }
}

void SensorService::sensorsOn() {
    if (!isCreated) {
        ESP_LOGW(SENSOR_TAG, "Sensor task not created");
        return;
    }

    ESP_LOGI(SENSOR_TAG, "Sensors on");

    running = true;

    // Notify the sending task to start
    xTaskNotifyGive(sending_TaskHandle);
}

void SensorService::sensorsOff() {
    if (!isCreated) {
        ESP_LOGW(SENSOR_TAG, "Sensor task not created");
        return;
    }

    ESP_LOGI(SENSOR_TAG, "Sensors off");

    running = false;
}

void SensorService::createSendingTask() {
    BaseType_t res = xTaskCreatePinnedToCore(
        sendingLoop, /* Function to implement the task */
        "SendingTask", /* Name of the task */
        6000,  /* Stack size in words */
        NULL,  /* Task input parameter */
        1,  /* Priority of the task */
        &sending_TaskHandle,  /* Task handle. */
        0); /* Core where the task should run */

    if (res != pdPASS) {
        ESP_LOGE(SENSOR_TAG, "Sending task creation failed");
        return;
    }

    ESP_LOGI(SENSOR_TAG, "Sending task created");

    isCreated = true;
}

void SensorService::sendingLoop(void* parameter) {
    SensorService& sensorService = SensorService::getInstance();
    UBaseType_t uxHighWaterMark;

    while (true) {
        if (!sensorService.running) {
            // Wait until a notification to start the task
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        else {
            uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGD(SENSOR_TAG, "Stack space unused after entering the task: %d", uxHighWaterMark);

            sensorService.createAndSendMessage();

            vTaskDelay(SENSOR_SENDING_EVERY / portTICK_PERIOD_MS);

            // Print the free heap memory
            ESP_LOGD(SENSOR_TAG, "Free heap: %d", esp_get_free_heap_size());
        }
    }
}

void SensorService::createAndSendMessage() {
    ESP_LOGV(SENSOR_TAG, "Sending sensor data %d", sensorMessageId++);

    MeasurementMessage* message = new MeasurementMessage();

    message->sensorCommand = SensorCommand::Data;

    // Get GPS data
    message->gps = GPSService::getInstance().getGPSMessage();

    // Get sensor data
    message->phSensorMessage = phSensor->read();
    message->sht4xAirSensorMessage = sht4xAirSensor->read();
    message->soilSensorMessage = soilSensor->read();
    message->waterLevelSensorMessage = waterLevelSensor->read();

    message->appPortDst = appPort::MQTTApp;
    message->appPortSrc = appPort::SensorApp;
    message->addrSrc = LoraMesher::getInstance().getLocalAddress();
    message->addrDst = 0;
    message->messageId = sensorMessageId;

    message->messageSize = sizeof(MeasurementMessage) - sizeof(DataMessageGeneric);

    // Send the message
    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*) message);

    // Delete the message
    delete message;
}
