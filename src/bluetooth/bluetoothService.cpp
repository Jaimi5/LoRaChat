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

void BluetoothService::writeToBluetooth(String message) {
    SerialBT->println(message);
}

void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
    BluetoothService& instance = BluetoothService::getInstance();
    if (event == ESP_SPP_SRV_OPEN_EVT && instance.SerialBT->hasClient()) {
        Log.verboseln("Bluetooth Connected");
        String help = instance.commandService->helpCommand();
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
        message.replace("\n", "");
        Serial.println(message);
        String executedProgram = commandService->execute(message);
        Serial.println(executedProgram);
        writeToBluetooth(executedProgram);
    }
}
//         String message = SerialBT->readStringUntil('\n');
//         if (!message.isEmpty() || message != String('\n')) {
//             message.remove(message.length() - 1, 2);
//             if (message.indexOf("Back") != -1 || message.indexOf("back") != -1) {
//                 SerialBT->println("Going back");
//                 state = 0;
//                 continue;
//             }

//             switch (state) {
//                 case 0:
//                     Serial.println("BT: " + message);

//                     vTaskDelay(50);
//                     if (message.indexOf("help") != -1) printHelp();
//                     else if (message.indexOf("get location") != -1) printCurrentLocationBT();
//                     else if (message.indexOf("search contacts") != -1) searchContacts();
//                     else if (message.indexOf("print contacts") != -1) printContacts();
//                     else if (message.indexOf("change name") != -1) {
//                         SerialBT->println("Please write your new name. Write back to cancel");
//                         state = 1;
//                     }
//                     else if (message.indexOf("chat") != -1) {
//                         printContacts();
//                         SerialBT->println("Write name to chat. Write back to go back");
//                         state = 2;
//                     }
//                     else if (message.indexOf("print RT") != -1) printRoutingTable();
//                     else if (message.indexOf("clear") != -1) clearDisplay();
//                     else {
//                         SerialBT->println("Command not found");
//                     }
//                     break;
//                 case 1:
//                     if (message.length() <= MAX_NAME_LENGTH) {

//                         changeName(message);
//                         SerialBT->println("Hello " + message + "!");
//                         state = 0;
//                     }
//                     else {
//                         SerialBT->println("MAX NAME LENGTH is " + String(MAX_NAME_LENGTH));
//                     }
//                     break;
//                 case 2:
//                     if (message.length() <= MAX_NAME_LENGTH) {
//                         chatAddr = contactService.getAddrContact(message);
//                         if (chatAddr == 0) {
//                             SerialBT->println("Name not found in the contact list, here is your options:");
//                             printContacts();
//                             continue;
//                         }

//                         chatName = message;
//                         SerialBT->println("Starting to chat to " + message + "!");
//                         state = 3;
//                     }
//                     else {
//                         SerialBT->println("MAX NAME LENGTH is " + String(MAX_NAME_LENGTH));
//                     }
//                     break;

//                 case 3:
//                     sendMessage(chatAddr, message);
//             }
//         }
//     }
// }