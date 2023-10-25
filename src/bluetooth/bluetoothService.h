#pragma once

#include <Arduino.h>

//Bluetooth
#include <BluetoothSerial.h>

#include "bluetoothCommandService.h"

#include "message/messageService.h"

#include "message/messageManager.h"

#include "helpers/helper.h"

//TODO: Check for wake from sleep mode.
//TODO: Check for max characters in a message to avoid buffer overflow.

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

    BluetoothSerial* SerialBT = new BluetoothSerial();

    BluetoothCommandService* bluetoothCommandService = new BluetoothCommandService();

    virtual void processReceivedMessage(messagePort port, DataMessage* message);

    bool hasClient = false;

    void disconnect();

private:

    BluetoothService(): MessageService(appPort::BluetoothApp, String("Bluetooth")) {
        commandService = bluetoothCommandService;
    };

    void createBluetoothTask();

    static void BluetoothLoop(void*);

    TaskHandle_t bluetooth_TaskHandle = NULL;

    String localName = "";

};
