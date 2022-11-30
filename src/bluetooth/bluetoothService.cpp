#include "bluetoothService.h"

/**
 * @brief Create a Bluetooth Task
 *
 */
void BluetoothService::createBluetoothTask() {
    int res = xTaskCreate(
        BluetoothLoop,
        "Bluetooth Task",
        4096,
        (void*) 1,
        2,
        &bluetooth_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Bluetooth task handle error: %d"), res);
    }
}

void BluetoothService::BluetoothLoop(void*) {
    BluetoothService& bluetoothService = BluetoothService::getInstance();
    for (;;) {
        bluetoothService.loop();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

bool BluetoothService::isDeviceConnected() {
    return SerialBT->hasClient();
}

bool BluetoothService::writeToBluetooth(String message) {
    Serial.println("Sending message to bluetooth: " + message);

    if (!isDeviceConnected()) {
        Serial.println("No bluetooth device connected");
        return false;
    }

    SerialBT->println(message);

    return true;
}

void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
    BluetoothService& instance = BluetoothService::getInstance();
    if (event == ESP_SPP_SRV_OPEN_EVT && instance.SerialBT->hasClient()) {
        Log.verboseln("Bluetooth Connected");
        String help = MessageManager::getInstance().getAvailableCommands();
        Serial.println(help);
        instance.writeToBluetooth(help);
    }
    else if (event == ESP_SPP_CLOSE_EVT && !instance.SerialBT->hasClient()) {
        Log.verboseln("Bluetooth Disconnected");
    }
}

void BluetoothService::initBluetooth(String lclName) {
    if (SerialBT->register_callback(callback) == ESP_OK) {
        Log.infoln(F("Bluetooth callback registered"));
    }
    else {
        Log.errorln(F("Bluetooth callback not registered"));
    }

    if (!SerialBT->begin(lclName)) {
        Log.errorln("BT init error");
    }

    localName = lclName;

    Serial.println("DeviceID: " + lclName);
    createBluetoothTask();
}

uint32_t state = 0;
String chatName = "";
uint16_t chatAddr = 0;

void BluetoothService::loop() {
    while (SerialBT->available()) {
        String message = SerialBT->readStringUntil('\n');
        message.remove(message.length() - 1, 1);
        Serial.println(message);
        String executedProgram = MessageManager::getInstance().executeCommand(message);
        Serial.println(executedProgram);
        writeToBluetooth(executedProgram);
    }
}

void BluetoothService::processReceivedMessage(messagePort port, DataMessage* message) {
    BluetoothMessage* bluetoothMessage = (BluetoothMessage*) message;
    switch (bluetoothMessage->type) {
        case BluetoothMessageType::bluetoothMessage:
            writeToBluetooth(Helper::uint8ArrayToString(bluetoothMessage->message, bluetoothMessage->getPayloadSize()));
            break;
        default:
            break;
    }
}