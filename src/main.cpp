#include <Arduino.h>

//Manager
#include "./message/messageManager.h"

//Display
#include "display.h"

//LoRaMesh
#include "loramesh/loraMeshService.h"

//Helpers
// #include "contacts.h"

//GPS libraries
#include "gps\gpsService.h"

//Bluetooth
#include "bluetooth\bluetoothService.h"

//Using LILYGO TTGO T-BEAM v1.1 
#define BOARD_LED   4
#define LED_ON      LOW
#define LED_OFF     HIGH



// #pragma region Display

// void displayHeader() {
//     Screen.changeLineTwo("Address: " + (String) radio.getLocalAddress());
// }

// #pragma endregion

#pragma region HelperFunctions

/**
 * @brief Flash the lead
 *
 * @param flashes number of flashes
 * @param delaymS delay between is on and off of the LED
 */
void led_Flash(uint16_t flashes, uint16_t delaymS) {
    for (uint16_t index = 0; index < flashes; index++) {
        digitalWrite(BOARD_LED, LED_OFF);
        vTaskDelay(delaymS / portTICK_PERIOD_MS);
        digitalWrite(BOARD_LED, LED_ON);
        vTaskDelay(delaymS / portTICK_PERIOD_MS);
    }
}

#pragma endregion

// #pragma region MessageTypes

// enum messageType {
//     contactRequest,
//     contactResponse,
//     sendMessageType,
//     gpsMessage,
//     SOSMessageType
// };

// #pragma pack(1)
// class DataMessage {
// public:
//     messageType type;
// };

// class ContactResponseMessage: public DataMessage {
// public:
//     char name[MAX_NAME_LENGTH];
// };

// class Message: public DataMessage {
// public:
//     char payload[];
// };

// class GPSMessage: public DataMessage {
// public:
//     double latitude;
//     double longitude;
//     double altitude;
// };

// class SOSMessage: public GPSMessage {
// public:
//     char name[MAX_NAME_LENGTH];
// };

// #pragma pack()

// #pragma endregion

// #pragma region LoRaMesher

// void requestContactInfo(uint16_t addr) {
//     DataMessage* rqContact = new DataMessage();
//     rqContact->type = contactRequest;

//     radio.sendReliable(addr, rqContact, 1);
//     delete rqContact;
// }

// void responseContactInfo(uint16_t addr) {
//     ContactResponseMessage* rqContact = new ContactResponseMessage();
//     rqContact->type = contactResponse;
//     ((String) contactService.myName).toCharArray(rqContact->name, MAX_NAME_LENGTH);

//     radio.sendReliable(addr, rqContact, 1);
//     delete rqContact;
// }

// void sendMessage(uint16_t dst, String message) {
//     uint32_t messageSize = message.length() + sizeof(Message);
//     Message* mess = (Message*) malloc(messageSize);
//     memcpy(mess->payload, message.c_str(), message.length());
//     mess->type = sendMessageType;

//     radio.sendReliablePacket(dst, (uint8_t*) mess, messageSize);

//     free(mess);
// }

// void searchContacts() {
//     SerialBT.println("Begin to search contacts");
//     LM_LinkedList<RouteNode>* nodes = radio.routingTableList();
//     nodes->setInUse();
//     if (nodes->moveToStart()) {
//         do {
//             RouteNode* node = nodes->getCurrent();
//             if (node) {
//                 requestContactInfo(node->networkNode.address);
//             }
//         } while (nodes->next());
//     }

//     nodes->releaseInUse();
// }

// void printMessage(Message* m, size_t size, uint16_t src) {
//     led_Flash(4, 100);

//     String contactName = contactService.getNameContact(src);
//     if (contactName.isEmpty()) {
//         contactName = String(src);
//     }

//     String textMessage = String(m->payload);

//     //TODO: XDDDD sry
//     int index = textMessage.indexOf("xV");
//     textMessage.remove(index, index - textMessage.length());

//     Serial.println("Message arrived: " + contactName + ": " + textMessage);
//     SerialBT.println(contactName + ": " + textMessage);

//     Screen.changeLineFFour(contactName + ": " + textMessage);
// }


// /**
//  * @brief Print the routing table into the display
//  *
//  */
// void printRoutingTableToDisplay() {

//     //Set the routing table list that is being used and cannot be accessed (Remember to release use after usage)
//     LM_LinkedList<RouteNode>* routingTableList = radio.routingTableList();

//     routingTableList->setInUse();

//     Screen.changeSizeRouting(radio.routingTableSize());

//     char text[15];
//     for (int i = 0; i < radio.routingTableSize(); i++) {
//         RouteNode* rNode = (*routingTableList)[i];
//         NetworkNode node = rNode->networkNode;
//         snprintf(text, 15, ("|%X(%d)->%X"), node.address, node.metric, rNode->via);
//         Screen.changeRoutingText(text, i);
//     }

//     //Release routing table list usage.
//     routingTableList->releaseInUse();

//     Screen.changeLineFour();
// }

// void SOSMessageReceived(SOSMessage* sos, uint16_t src) {
//     // display.clearDisplay();
//     // displayHeader();

//     // display.setCursor(20, 27);
//     // display.setTextSize(4);
//     // display.println("SOS");

//     // display.setCursor(20, 45);
//     // display.println("GPS:" + getGPS(sos->latitude, sos->longitude, sos->altitude));
//     // display.setCursor(20, 54);
//     // display.println("FROM: " + String(sos->name) + " (" + String(src) + ")");
//     // display.display();
// }

// void clearDisplay() {
//     Screen.changeLineFFour("");
// }

// void printRoutingTable() {
//     //Set the routing table list that is being used and cannot be accessed (Remember to release use after usage)
//     LM_LinkedList<RouteNode>* routingTableList = radio.routingTableList();
//     SerialBT.println("---- Routing Table ----");
//     routingTableList->setInUse();


//     for (int i = 0; i < radio.routingTableSize(); i++) {
//         RouteNode* rNode = (*routingTableList)[i];
//         SerialBT.println(String(i) + " - Src:" + String(rNode->networkNode.address) + "|Metric: " + String(rNode->networkNode.metric) + "|Via: " + String(rNode->via));
//     }

//     if (radio.routingTableSize() == 0) {
//         SerialBT.println("Routing Table empty");
//     }


//     //Release routing table list usage.
//     routingTableList->releaseInUse();
// }

// #pragma endregion

#pragma region GPS

GPSService& gpsService = GPSService::getInstance();

TaskHandle_t gpsDisplay_TaskHandle = NULL;

void gpsDisplay_Task(void* pvParameters) {
    while (true) {
        String gpsString = gpsService.getGPSUpdatedWait();

        Screen.changeLineTwo(gpsString);
        vTaskDelay(50000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief Create a Receive Messages Task and add it to the LoRaMesher
 *
 */
void createUpdateGPSDisplay() {
    int res = xTaskCreate(
        gpsDisplay_Task,
        "Gps Display Task",
        4096,
        (void*) 1,
        2,
        &gpsDisplay_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("Gps Display Task creation gave error: %d"), res);
    }
}

void initializeGPS() {
    //Initialize GPS
    gpsService.initGPS();

    //Initialize GPS Display
    createUpdateGPSDisplay();
}

// // GPSMessage* createGPSMessage() {
// //     updateGPS();
// //     GPSMessage* gpMessage = new GPSMessage();
// //     gpMessage->altitude = gps.altitude.meters();
// //     gpMessage->latitude = gps.location.lat();
// //     gpMessage->longitude = gps.location.lng();

// //     return gpMessage;
// // }

// // void sendSOSMessage() {
// //     updateGPS();
// //     SOSMessage* sosMessage = new SOSMessage();
// //     sosMessage->altitude = gps.altitude.meters();
// //     sosMessage->latitude = gps.location.lat();
// //     sosMessage->longitude = gps.location.lng();
// //     ((String) contactService.myName).toCharArray(sosMessage->name, MAX_NAME_LENGTH);

// //     //Set the routing table list that is being used and cannot be accessed (Remember to release use after usage)
// //     LM_LinkedList<RouteNode>* routingTableList = radio.routingTableList();
// //     SerialBT.println("---- Routing Table ----");
// //     routingTableList->setInUse();


// //     for (int i = 0; i < radio.routingTableSize(); i++) {
// //         RouteNode* rNode = (*routingTableList)[i];
// //         radio.sendReliable(rNode->networkNode.address, sosMessage, 1);
// //     }

// //     //Release routing table list usage.
// //     routingTableList->releaseInUse();

// //     delete sosMessage;
// // }

#pragma endregion

// #pragma region Contacts

// void printContacts() {
//     SerialBT.print(contactService.getContactsString());
// }

// void changeName(String name) {
//     contactService.changeName(name);
//     //TODO: Notify contacts that the name has been changed
// }

// #pragma endregion

#pragma region SerialBT

BluetoothService& bluetoothService = BluetoothService::getInstance();

void initBluetooth() {
    bluetoothService.initBluetooth("a1");
}

#pragma endregion

#pragma region Manager

MessageManager& manager = MessageManager::getInstance();

void initManager() {
    manager.init();
    Log.verboseln("Manager initialized");

    manager.addMessageService(&bluetoothService);
    Log.verboseln("Bluetooth service added to manager");

    Serial.println(manager.getAvailableCommands());
}

#pragma endregion

void setup() {
    //initialize Serial Monitor
    Serial.begin(115200);
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);

    Screen.initDisplay();

    // initializeLoraMesher();

    initializeGPS();

    initBluetooth();

    initManager();

    pinMode(BOARD_LED, OUTPUT); //setup pin as output for indicator LED
    led_Flash(2, 100);
}

void loop() {
    Screen.drawDisplay();
    vTaskDelay(50 / portTICK_PERIOD_MS);
}