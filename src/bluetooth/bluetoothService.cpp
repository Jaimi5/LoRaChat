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
    for (;;) {
        bluetoothService.loop();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

bool BluetoothService::isDeviceConnected() {
    return BLE.connected();
}

bool BluetoothService::writeToBluetooth(String message) {
    ESP_LOGV(BLE_TAG, "Sending message to bluetooth: %s", message);

    if (!isDeviceConnected()) {
        ESP_LOGV(BLE_TAG, "No bluetooth device connected");
        return false;
    }

    return true;
}

void blePeripheralConnectHandler(BLEDevice central) {
    // central connected event handler
    ESP_LOGV(BLE_TAG, "Connected event, central: ");
    ESP_LOGV(BLE_TAG, "%s", central.address());
}

void blePeripheralDisconnectHandler(BLEDevice central) {
    // central disconnected event handler
    ESP_LOGV(BLE_TAG, "Disconnected event, central: ");
    ESP_LOGV(BLE_TAG, "%s", central.address());
}

void wifiNameCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic) {
    // central wrote new value to characteristic, update LED
    ESP_LOGV(BLE_TAG, "Characteristic event, written: ");


    // yes, get the value, characteristic is 1 byte so use byte value
    char value[30];

    characteristic.readValue(value, 30);


    ESP_LOGV(BLE_TAG, "%s", value);

    WiFiServerService& wiFiService = WiFiServerService::getInstance();

    wiFiService.addSSID(value);

    wiFiService.saveWiFiData();
    //TODO: here we need to save the new wifi name to the flash memory

}

void wifiPwdCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic) {
    // central wrote new value to characteristic, update LED
    ESP_LOGV(BLE_TAG, "Characteristic event, written: ");


    // yes, get the value
    char value[30];

    characteristic.readValue(value, 30);


    ESP_LOGV(BLE_TAG, "%s", value);

    WiFiServerService& wiFiService = WiFiServerService::getInstance();

    wiFiService.addPassword(value);


    wiFiService.saveWiFiData();


    //TODO: here we need to save the new wifi pwd to the flash memory

}

void BluetoothService::initBluetooth(String lclName) {

    // begin initialization
    if (!BLE.begin()) {
        ESP_LOGE(BLE_TAG, "starting Bluetooth® Low Energy module failed!");

        while (1);
    }

    // set the local name peripheral advertises
    BLE.setLocalName(lclName.c_str());
    // set the UUID for the service this peripheral advertises:
    BLE.setAdvertisedService(configService);

    // add the characteristics to the service
    configService.addCharacteristic(wifiNameCharacteristic);
    configService.addCharacteristic(wifiPwdCharacteristic);

    // add the service
    BLE.addService(configService);

    // set the initial value for the characeristic: TODO: here we need to take thename of the currently saved wifi

    WiFiServerService& wiFiService = WiFiServerService::getInstance();

    wifiNameCharacteristic.writeValue(wiFiService.getSSID().c_str());

    wifiPwdCharacteristic.writeValue(wiFiService.getPassword().c_str());


    // assign event handlers for connected, disconnected to peripheral
    BLE.setEventHandler(BLEConnected, blePeripheralConnectHandler);
    BLE.setEventHandler(BLEDisconnected, blePeripheralDisconnectHandler);

    // assign event handlers for characteristic
    wifiNameCharacteristic.setEventHandler(BLEWritten, wifiNameCharacteristicWritten);
    wifiPwdCharacteristic.setEventHandler(BLEWritten, wifiPwdCharacteristicWritten);

    // start advertising
    BLE.advertise();

    ESP_LOGI(BLE_TAG, "Bluetooth® device active, waiting for connections...");

    localName = lclName;

    createBluetoothTask();
}

uint32_t state = 0;
String chatName = "";
uint16_t chatAddr = 0;

void BluetoothService::loop() {
    // poll for Bluetooth® Low Energy events
    BLE.poll();
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