#include "bluetoothService.h"

static const char* BLE_TAG = "BluetoothService";

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
        ESP_LOGE(BLE_TAG, "Bluetooth task handle error: %d", res);
    }
}

void BluetoothService::BluetoothLoop(void*) {
    BluetoothService& bluetoothService = BluetoothService::getInstance();
    ESP_LOGV(BLE_TAG, "Stack space unused after entering the task: %d", uxTaskGetStackHighWaterMark(NULL));
    for (;;) {

        bluetoothService.loop();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

bool BluetoothService::isDeviceConnected() {
    return SerialBT->hasClient();
}

bool BluetoothService::writeToBluetooth(String message) {
    ESP_LOGV(BLE_TAG, "Sending message to bluetooth: %s", message.c_str());

    if (!isDeviceConnected()) {
        ESP_LOGV(BLE_TAG, "No bluetooth device connected");
        return false;
    }

    SerialBT->println(message);

    return true;
}

void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
    BluetoothService& instance = BluetoothService::getInstance();
    if (event == ESP_SPP_SRV_OPEN_EVT && instance.SerialBT->hasClient()) {
        ESP_LOGV(BLE_TAG, "Bluetooth Connected");
        instance.hasClient = true;
        String help = MessageManager::getInstance().getAvailableCommands();
        Serial.println(help);
        instance.writeToBluetooth(help);
    }
    else if (event == ESP_SPP_CLOSE_EVT && !instance.SerialBT->hasClient()) {
        ESP_LOGV(BLE_TAG, "Bluetooth Disconnected");
        // TODO: Bluetooth and WiFi should not be used at the same time
    }
}

void BluetoothService::initBluetooth(String lclName) {
    if (SerialBT->register_callback(callback) == ESP_OK)
        ESP_LOGV(BLE_TAG, "Bluetooth callback registered");
    else
        ESP_LOGE(BLE_TAG, "Bluetooth callback not registered");

    if (!SerialBT->begin(lclName))
        ESP_LOGE(BLE_TAG, "BT init error");

    localName = lclName;

    ESP_LOGV(BLE_TAG, "DeviceID: %s", lclName.c_str());
    createBluetoothTask();
}

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

void BluetoothService::disconnect() {
    ESP_LOGE(BLE_TAG, "Bluetooth task deleted");
    // Disconnect all the bluetooth connections to no affect the wifi
    SerialBT->end();
    SerialBT->disconnect();

    delete SerialBT;
    vTaskDelete(bluetooth_TaskHandle);
}
