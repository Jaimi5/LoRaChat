#pragma once

#include <Arduino.h>

//Bluetooth
#include <ArduinoBLE.h>

#include <ArduinoLog.h>

#include "bluetoothCommandService.h"

#include "message/messageService.h"

#include "message/messageManager.h"

#include "helpers/helper.h"

class BluetoothService: public MessageService {
public:
    /**
     * @brief Construct a new BluetoothService object
     *
     */
    static BluetoothService& getInstance() {
        static BluetoothService instance;
        return instance;
    }

    void initBluetooth(String localName);

    void loop();

    bool isDeviceConnected();

    bool writeToBluetooth(String message);

    BluetoothCommandService* bluetoothCommandService = new BluetoothCommandService();

    virtual void processReceivedMessage(messagePort port, DataMessage* message);

private:

    BluetoothService(): MessageService(appPort::BluetoothApp, String("Bluetooth")) {
        commandService = bluetoothCommandService;
    };

    BLEService configService = BLEService("0d38784b-07a0-4642-9f14-a460a538104b"); // create service

    // create switch characteristic and allow remote device to read and write
    BLECharacteristic wifiNameCharacteristic = BLECharacteristic("85b96737-8b13-4d84-8d42-3d98bba40f07", BLERead | BLEWrite, "VeryLongPasswordToStayFresh");

    BLECharacteristic wifiPwdCharacteristic = BLECharacteristic("6e118941-a653-4ecb-98e1-b4710d216a1a", BLERead | BLEWrite, "VeryLongPasswordToStayFresh");


    void createBluetoothTask();

    static void BluetoothLoop(void*);

    TaskHandle_t bluetooth_TaskHandle = NULL;

    String localName = "";

};
