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

    BLEService configService = BLEService("19B10010-E8F2-537E-4F6C-D104768A1214"); // create service

    // create switch characteristic and allow remote device to read and write
    BLECharacteristic wifiNameCharacteristic = BLECharacteristic("19B10011-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite, "VeryLongPasswordToStayFresh");

    void createBluetoothTask();

    static void BluetoothLoop(void*);

    TaskHandle_t bluetooth_TaskHandle = NULL;

    String localName = "";

};
