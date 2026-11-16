#pragma once

#include <Arduino.h>

// Bluetooth
#include <BluetoothSerial.h>

#include "bluetoothCommandService.h"

#include "message/messageService.h"

#include "message/messageManager.h"

#include "helpers/helper.h"

#include "lineReader.h"

// TODO: Check for wake from sleep mode.

// A partial line is dropped when no byte arrives for this long.
#define BT_LINE_IDLE_TIMEOUT_MS 5000

class BluetoothService : public MessageService {
public:
    /**
     * @brief Construct a new BluetoothService object
     *
     */
    static BluetoothService& getInstance() {
        static BluetoothService instance;
        return instance;
    }

    ~BluetoothService() {
        if (bluetoothCommandService != nullptr) {
            delete bluetoothCommandService;
        }
    }

    void initBluetooth(String localName);

    void loop();

    bool isDeviceConnected();

    bool writeToBluetooth(String message);

    /**
     * @brief Write a line to the connected device.
     *
     * The text and its line ending are queued as a single packet, so lines written from
     * different tasks are not interleaved. Safe to call from any task.
     *
     * @return false if no device is connected or the line could not be queued
     */
    bool writeLine(const char* text);

    BluetoothSerial* SerialBT = new BluetoothSerial();

    BluetoothCommandService* bluetoothCommandService = nullptr;

    virtual void processReceivedMessage(messagePort port, DataMessage* message);

    bool hasClient = false;

    // Set by the Bluetooth stack callback and handled in the Bluetooth task, because the
    // callback runs on the small stack of the Bluetooth stack.
    volatile bool connectPending = false;
    volatile bool disconnectPending = false;

    void disconnect();

private:
    BluetoothService() : MessageService(appPort::BluetoothApp, String("Bluetooth")) {
        bluetoothCommandService = new BluetoothCommandService();
        commandService = bluetoothCommandService;
    };

    void createBluetoothTask();

    static void BluetoothLoop(void*);

    TaskHandle_t bluetooth_TaskHandle = NULL;

    String localName = "";

    LineReader lineReader;

    uint32_t lastByteMs = 0;

    bool stackReported = false;

    void handleLine(const char* line);
};
