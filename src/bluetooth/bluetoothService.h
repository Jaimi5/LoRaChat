#pragma once

#include <Arduino.h>

//Bluetooth
#include <BluetoothSerial.h>

#include <ArduinoLog.h>

#include "bluetoothCommandService.h"

class BluetoothService {
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

    void writeToBluetooth(String message);

    BluetoothSerial* SerialBT = new BluetoothSerial();

    BluetoothCommandService* commandService = new BluetoothCommandService();

private:

    BluetoothService() {};

    TaskHandle_t bluetooth_TaskHandle = NULL;

    void createBluetoothTask();

    static void BluetoothLoop(void*);

    String localName = "";
};
