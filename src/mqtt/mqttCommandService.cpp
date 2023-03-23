#include "mqttCommandService.h"
#include "mqttService.h"

MqttCommandService::MqttCommandService() {
    // Send command to bluetooth
    addCommand(Command("/sendB", "Send a message to the mqtt device", MqttMessageType::mqttMessage, 1,
        [this](String args) {
        return MqttService::getInstance().writeToMqtt(args) ? "Message sent" : "Device not connected";
    }));
}