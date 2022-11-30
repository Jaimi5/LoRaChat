#include "messageService.h"

MessageService::MessageService(uint8_t id, String name) {
    serviceId = id;
    serviceName = name;
    // xQueueReceived = xQueueCreate(10, sizeof(DataMessage*));
}

String MessageService::toString() {
    return "Id: " + String(serviceId) + " - " + serviceName;
}

// void MessageService::init() {
//     int res = xTaskCreate(
//         loopReceivedMessages,
//         "Bluetooth Task",
//         4096,
//         (void*) 1,
//         2,
//         &receiveMessage_TaskHandle);
//     if (res != pdPASS) {
//         Log.errorln(F("Bluetooth task handle error: %d"), res);
//     }
// }