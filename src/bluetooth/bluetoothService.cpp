#include "config.h"

#ifdef BLUETOOTH_ENABLED

#include "bluetoothService.h"

// The console uses the Bluetooth Classic Serial Port Profile. Stop the build on boards where it
// cannot work instead of failing at run time.
#if !defined(CONFIG_BT_SPP_ENABLED)
#error "BLUETOOTH_ENABLED needs CONFIG_BT_SPP_ENABLED, which the sdkconfig of this board lacks"
#endif
#if defined(CONFIG_BTDM_CTRL_MODE_BLE_ONLY)
#error "BLUETOOTH_ENABLED needs a Bluetooth Classic controller, the sdkconfig sets BLE only"
#endif

static const char* BLE_TAG = "BluetoothService";

/**
 * @brief Create a Bluetooth Task
 *
 */
void BluetoothService::createBluetoothTask() {
    int res =
        xTaskCreate(BluetoothLoop, "Bluetooth Task", 8192, (void*)1, 2, &bluetooth_TaskHandle);
    if (res != pdPASS) {
        ESP_LOGE(BLE_TAG, "Bluetooth task handle error: %d", res);
    }
}

void BluetoothService::BluetoothLoop(void*) {
    BluetoothService& bluetoothService = BluetoothService::getInstance();
    ESP_LOGV(BLE_TAG, "Stack space unused after entering the task: %d",
             uxTaskGetStackHighWaterMark(NULL));
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

    // One write, so the text and its line ending are queued as a single packet.
    message += "\r\n";

    return SerialBT->write((const uint8_t*)message.c_str(), message.length()) == message.length();
}

bool BluetoothService::writeLine(const char* text) {
    if (text == nullptr || !isDeviceConnected())
        return false;

    size_t length = strlen(text);
    if (length > BT_LINE_MAX + 64)
        length = BT_LINE_MAX + 64;

    char line[BT_LINE_MAX + 64 + 2];
    memcpy(line, text, length);
    line[length] = '\r';
    line[length + 1] = '\n';

    return SerialBT->write((const uint8_t*)line, length + 2) == length + 2;
}

void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
    // This runs in the task of the Bluetooth stack: only record the event here.
    BluetoothService& instance = BluetoothService::getInstance();
    if (event == ESP_SPP_SRV_OPEN_EVT) {
        instance.connectPending = true;
    } else if (event == ESP_SPP_CLOSE_EVT) {
        instance.disconnectPending = true;
    }
}

void BluetoothService::initBluetooth(String lclName) {
    if (SerialBT->register_callback(callback) == ESP_OK)
        ESP_LOGV(BLE_TAG, "Bluetooth callback registered");
    else
        ESP_LOGE(BLE_TAG, "Bluetooth callback not registered");

    if (!SerialBT->begin(lclName)) {
        ESP_LOGE(BLE_TAG, "BT init error, the Bluetooth console is not available");
        return;
    }

    localName = lclName;

    ESP_LOGV(BLE_TAG, "DeviceID: %s", lclName.c_str());
    createBluetoothTask();
}

void BluetoothService::loop() {
    if (disconnectPending) {
        disconnectPending = false;
        ESP_LOGV(BLE_TAG, "Bluetooth Disconnected");
        hasClient = false;
        // A half-typed line belongs to the device that has just left.
        lineReader.reset();
        // TODO: Bluetooth and WiFi should not be used at the same time
    }

    if (connectPending) {
        connectPending = false;
        if (SerialBT->hasClient()) {
            ESP_LOGV(BLE_TAG, "Bluetooth Connected");
            hasClient = true;
            lineReader.reset();
            String help = MessageManager::getInstance().getAvailableCommands();
            Serial.println(help);
            writeToBluetooth(help);
        }
    }

    while (SerialBT->available()) {
        int c = SerialBT->read();
        if (c < 0)
            break;

        lastByteMs = millis();

        switch (lineReader.push((uint8_t)c)) {
            case LineReader::Line:
                handleLine(lineReader.line());
                break;
            case LineReader::TooLong:
                writeToBluetooth("Line too long (max " + String(BT_LINE_MAX) +
                                 " characters), ignored");
                break;
            default:
                break;
        }
    }

    if (lineReader.pending() && millis() - lastByteMs > BT_LINE_IDLE_TIMEOUT_MS)
        lineReader.reset();
}

void BluetoothService::handleLine(const char* line) {
    Serial.println(line);

    String result = MessageManager::getInstance().executeCommand(line);

    if (result.length() > 0) {
        Serial.println(result);
        writeToBluetooth(result);
    }

    if (!stackReported) {
        stackReported = true;
        ESP_LOGI(BLE_TAG, "Bluetooth task stack unused after the first command: %d",
                 uxTaskGetStackHighWaterMark(NULL));
    }
}

void BluetoothService::processReceivedMessage(messagePort port, DataMessage* message) {
    BluetoothMessage* bluetoothMessage = (BluetoothMessage*)message;

    // The payload must at least hold the message type.
    if (bluetoothMessage->messageSize < sizeof(BluetoothMessageType))
        return;

    switch (bluetoothMessage->type) {
        case BluetoothMessageType::bluetoothMessage:
            writeToBluetooth(Helper::uint8ArrayToString(bluetoothMessage->message,
                                                        bluetoothMessage->getPayloadSize()));
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

#endif  // BLUETOOTH_ENABLED
