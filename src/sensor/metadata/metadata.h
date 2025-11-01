#pragma once

#include <Arduino.h>

#include "config.h"

#include "message/messageService.h"

#include "message/messageManager.h"

#include "sensor/metadata/metadataMessage.h"

#include "sensor/metadata/metadataCommandService.h"

#include "gps/gpsService.h"

#include "battery/battery.h"

class Metadata : public MessageService {
public:
    static Metadata& getInstance() {
        static Metadata instance;
        return instance;
    }

    ~Metadata() {
        if (metadataCommandService != nullptr) {
            delete metadataCommandService;
        }
    }

    void initMetadata();

    void startMetadata();

    void stopMetadata();

    void createAndSendMetadata();

    String getJSON(DataMessage* message);

    MetadataCommandService* metadataCommandService = nullptr;

private:
    Metadata() : MessageService(MetadataApp, "Metadata") {
        metadataCommandService = new MetadataCommandService();
        commandService = metadataCommandService;
    };

    TaskHandle_t metadata_TaskHandle = NULL;

    void createMetadataTask();

    static void metadataLoop(void*);

    bool running = true;

    uint8_t metadataId = 0;

    void getJSONDataObject(JsonObject& doc, MetadataMessage* metadataMessage);

    void getJSONSignObject(JsonObject& doc, MetadataMessage* metadataMessage);
};