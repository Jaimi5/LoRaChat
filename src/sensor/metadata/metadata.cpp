#include "metadata.h"

static const char* METADATA_TAG = "MetadataService";

void Metadata::initMetadata() {
    ESP_LOGI(METADATA_TAG, "Initializing metadata");

    createMetadataTask();

    ESP_LOGI(METADATA_TAG, "Metadata initialized");

    startMetadata();
}

void Metadata::startMetadata() {
    ESP_LOGI(METADATA_TAG, "Starting metadata");
    running = true;
    xTaskNotifyGive(metadata_TaskHandle);
}

void Metadata::stopMetadata() {
    ESP_LOGI(METADATA_TAG, "Stopping metadata");
    running = false;
}

String Metadata::getJSON(DataMessage* message) {
    MetadataMessage* metadataMessage = (MetadataMessage*) message;
    DynamicJsonDocument doc(1024);
    JsonObject jsonObj = doc.to<JsonObject>();
    JsonObject dataObj = jsonObj.createNestedObject("data");

    getJSONDataObject(dataObj, metadataMessage);

    String json;
    serializeJson(doc, json);

    return json;
}


void Metadata::createMetadataTask() {
    int res = xTaskCreate(
        metadataLoop,
        "Metadata Task",
        4096,
        (void*) 1,
        2,
        &metadata_TaskHandle
    );

    if (res != pdPASS) {
        ESP_LOGE(METADATA_TAG, "Failed to create metadata task");
    }
}

void Metadata::metadataLoop(void* pvParameters) {
    ESP_LOGI(METADATA_TAG, "Metadata task started");

    Metadata& metadata = Metadata::getInstance();

    while (true) {
        if (!metadata.running)
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        else {
            ESP_LOGV(METADATA_TAG, "Stack space unused after entering the task: %d", uxTaskGetStackHighWaterMark(NULL));

            metadata.createAndSendMetadata();
            vTaskDelay(METADATA_UPDATE_DELAY / portTICK_PERIOD_MS);
            ESP_LOGD(METADATA_TAG, "Free heap: %d", esp_get_free_heap_size());
        }
    }
}

void Metadata::createAndSendMetadata() {
    ESP_LOGV(METADATA_TAG, "Sending metadata message %d", metadataId++);

    // uint8_t metadataSize = 1; //TODO: This should be dynamic with an array of sensors
    // uint16_t metadataSensorSize = metadataSize * sizeof(MetadataSensorMessage);
    uint16_t messageWithHeaderSize = sizeof(MetadataMessage);// + metadataSensorSize;

    MetadataMessage* message = new MetadataMessage();

    message->appPortDst = appPort::MQTTApp;
    message->appPortSrc = appPort::MetadataApp;
    message->addrSrc = LoraMesher::getInstance().getLocalAddress();
    message->addrDst = 0;
    message->messageId = metadataId;

    message->messageSize = messageWithHeaderSize - sizeof(DataMessageGeneric);
    message->gps = GPSService::getInstance().getGPSMessage();
    message->metadataSendTimeInterval = METADATA_UPDATE_DELAY;
    message->batteryPercentage = Battery::getInstance().getVoltagePercentage();

    // message->metadataSize = metadataSize;

    //TODO: This should be a vector of sensors
    // Temperature& temperature = Temperature::getInstance();

    // MetadataSensorMessage* tempMetadata = temperature.getMetadataMessage();
    // memcpy(message->sensorMetadata, tempMetadata, sizeof(MetadataSensorMessage));

    MessageManager::getInstance().sendMessage(messagePort::MqttPort, (DataMessage*) message);

    delete(message);

}

void Metadata::getJSONDataObject(JsonObject& doc, MetadataMessage* metadataMessage) {
    metadataMessage->serialize(doc);
}

void Metadata::getJSONSignObject(JsonObject& doc, MetadataMessage* metadataMessage) {
    // metadataMessage->serializeSignature(doc);
}
