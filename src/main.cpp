#include <Arduino.h>

//GPS libraries
#include <SPI.h>
#include <axp20x.h>
#include <TinyGPSPlus.h>

//Display
#include "display.h"

//Bluetooth
#include <BluetoothSerial.h>

//LoRaMesher 
#include <LoraMesher.h>

//Helpers
#include "contacts.h"
#include "helpers.h"

//OLED pins
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define DISP_ADDRESS 0x3C // Address 0x3D for 128x64
#define OLED_RST  16

//Using LILYGO TTGO T-BEAM v1.1 
#define BOARD_LED   4
#define LED_ON      LOW
#define LED_OFF     HIGH

BluetoothSerial SerialBT;

LoraMesher& radio = LoraMesher::getInstance();

#pragma region Display

void displayHeader() {
    Screen.changeLineTwo("Address: " + (String) radio.getLocalAddress());
}

#pragma endregion

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

#pragma region MessageTypes

enum messageType {
    contactRequest,
    contactResponse,
    sendMessageType,
    gpsMessage,
    SOSMessageType
};

#pragma pack(1)
class DataMessage {
public:
    messageType type;
};

class ContactResponseMessage : public DataMessage {
public:
    char name[MAX_NAME_LENGTH];
};

class Message : public DataMessage {
public:
    char payload[];
};

class GPSMessage : public DataMessage {
public:
    double latitude;
    double longitude;
    double altitude;
};

class SOSMessage : public GPSMessage {
public:
    char name[MAX_NAME_LENGTH];
};

#pragma pack()

#pragma endregion

#pragma region LoRaMesher

void requestContactInfo(uint16_t addr) {
    DataMessage* rqContact = new DataMessage();
    rqContact->type = contactRequest;

    radio.sendReliable(addr, rqContact, 1);
    delete rqContact;
}

void responseContactInfo(uint16_t addr) {
    ContactResponseMessage* rqContact = new ContactResponseMessage();
    rqContact->type = contactResponse;
    ((String) contactService.myName).toCharArray(rqContact->name, MAX_NAME_LENGTH);

    radio.sendReliable(addr, rqContact, 1);
    delete rqContact;
}

void sendMessage(uint16_t dst, String message) {
    uint32_t messageSize = message.length() + sizeof(Message);
    Message* mess = (Message*) malloc(messageSize);
    memcpy(mess->payload, message.c_str(), message.length());
    mess->type = sendMessageType;

    radio.sendReliablePacket(dst, (uint8_t*) mess, messageSize);

    free(mess);
}

void searchContacts() {
    SerialBT.println("Begin to search contacts");
    LM_LinkedList<RouteNode>* nodes = radio.routingTableList();
    nodes->setInUse();
    if (nodes->moveToStart()) {
        do {
            RouteNode* node = nodes->getCurrent();
            if (node) {
                requestContactInfo(node->networkNode.address);
            }
        } while (nodes->next());
    }

    nodes->releaseInUse();
}

void printMessage(Message* m, size_t size, uint16_t src) {
    led_Flash(4, 100);

    String contactName = contactService.getNameContact(src);
    if (contactName.isEmpty()) {
        contactName = String(src);
    }

    String textMessage = String(m->payload);

    //TODO: XDDDD sry
    int index = textMessage.indexOf("xV");
    textMessage.remove(index, index - textMessage.length());

    Serial.println("Message arrived: " + contactName + ": " + textMessage);
    SerialBT.println(contactName + ": " + textMessage);

    Screen.changeLineFFour(contactName + ": " + textMessage);
}


String getGPS(double lat, double lng, double alt) {
    return "Lat: " + String(lat, 7) + " LONG: " + String(lng, 7) + " ALT: " + alt;
}

/**
 * @brief Print the routing table into the display
 *
 */
void printRoutingTableToDisplay() {

    //Set the routing table list that is being used and cannot be accessed (Remember to release use after usage)
    LM_LinkedList<RouteNode>* routingTableList = radio.routingTableList();

    routingTableList->setInUse();

    Screen.changeSizeRouting(radio.routingTableSize());

    char text[15];
    for (int i = 0; i < radio.routingTableSize(); i++) {
        RouteNode* rNode = (*routingTableList)[i];
        NetworkNode node = rNode->networkNode;
        snprintf(text, 15, ("|%X(%d)->%X"), node.address, node.metric, rNode->via);
        Screen.changeRoutingText(text, i);
    }

    //Release routing table list usage.
    routingTableList->releaseInUse();

    Screen.changeLineFour();
}

void SOSMessageReceived(SOSMessage* sos, uint16_t src) {
    // display.clearDisplay();
    // displayHeader();

    // display.setCursor(20, 27);
    // display.setTextSize(4);
    // display.println("SOS");

    // display.setCursor(20, 45);
    // display.println("GPS:" + getGPS(sos->latitude, sos->longitude, sos->altitude));
    // display.setCursor(20, 54);
    // display.println("FROM: " + String(sos->name) + " (" + String(src) + ")");
    // display.display();
}

void processReceivedMessage(AppPacket<DataMessage>* message) {
    DataMessage* dm = message->payload;
    switch (dm->type) {
        case contactRequest:
            responseContactInfo(message->src);
            break;
        case contactResponse:
            {
                ContactResponseMessage* cm = reinterpret_cast<ContactResponseMessage*>(dm);
                contactService.addContact(cm->name, message->src);
                SerialBT.println("New contact added (" + String(message->src) + ") " + String(cm->name));
            }
            break;
        case sendMessageType:
            {
                Message* payload = reinterpret_cast<Message*>(dm);
                printMessage(payload, message->getPayloadLength() - sizeof(Message), message->src);
            }
            break;
        case SOSMessageType:
            {
                SOSMessageReceived(reinterpret_cast<SOSMessage*>(dm), message->src);
            }
            break;
        default:
            break;
    }
}

/**
 * @brief Function that process the received packets
 *
 */
void processReceivedPackets(void*) {
    for (;;) {
        /* Wait for the notification of processReceivedPackets and enter blocking */
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);
        led_Flash(2, 100); //one quick LED flashes to indicate a packet has arrived

        //Iterate through all the packets inside the Received User Packets FiFo
        while (radio.getReceivedQueueSize() > 0) {
            Log.traceln(F("ReceivedUserData_TaskHandle notify received"));
            Log.traceln(F("Queue receiveUserData size: %d"), radio.getReceivedQueueSize());

            //Get the first element inside the Received User Packets FiFo
            AppPacket<DataMessage>* packet = radio.getNextAppPacket<DataMessage>();

            processReceivedMessage(packet);

            //Delete the packet when used. It is very important to call this function to release the memory of the packet.
            radio.deletePacket(packet);

        }
    }
}

void initializeLoraMesher() {
    radio.init(processReceivedPackets);
    Log.verboseln("LoraMesher initialized");

    // Display Header
    displayHeader();
    contactService.changeName(String(radio.getLocalAddress()));
}

void clearDisplay() {
    Screen.changeLineFFour("");
}

void printRoutingTable() {
    //Set the routing table list that is being used and cannot be accessed (Remember to release use after usage)
    LM_LinkedList<RouteNode>* routingTableList = radio.routingTableList();
    SerialBT.println("---- Routing Table ----");
    routingTableList->setInUse();


    for (int i = 0; i < radio.routingTableSize(); i++) {
        RouteNode* rNode = (*routingTableList)[i];
        SerialBT.println(String(i) + " - Src:" + String(rNode->networkNode.address) + "|Metric: " + String(rNode->networkNode.metric) + "|Via: " + String(rNode->via));
    }

    if (radio.routingTableSize() == 0) {
        SerialBT.println("Routing Table empty");
    }


    //Release routing table list usage.
    routingTableList->releaseInUse();
}

#pragma endregion

#pragma region GPS
HardwareSerial GPS(1);
AXP20X_Class axp;

// The TinyGPSPlus object
TinyGPSPlus gps;

void initGPS() {
    if (!axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
        Serial.println("AXP192 Begin PASS");
    }
    else {
        Serial.println("AXP192 Begin FAIL");
    }
    axp.setPowerOutPut(AXP192_LDO2, AXP202_ON);
    axp.setPowerOutPut(AXP192_LDO3, AXP202_ON);
    axp.setPowerOutPut(AXP192_DCDC2, AXP202_ON);
    axp.setPowerOutPut(AXP192_EXTEN, AXP202_ON);
    axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON);
    GPS.begin(9600, SERIAL_8N1, 34, 12);
}

bool isGPSValid() {
    if (gps.location.lat() == 0.000 || gps.location.lng() == 0.000) {
        return false;
    }

    return true;
}

static void smartDelay(unsigned long ms) {
    unsigned long start = millis();
    do {
        while (GPS.available())
            gps.encode(GPS.read());
    } while (millis() - start < ms);
}

void updateGPS() {
    smartDelay(400);
}

GPSMessage* createGPSMessage() {
    updateGPS();
    GPSMessage* gpMessage = new GPSMessage();
    gpMessage->altitude = gps.altitude.meters();
    gpMessage->latitude = gps.location.lat();
    gpMessage->longitude = gps.location.lng();

    return gpMessage;
}

void printCurrentLocation() {
    updateGPS();
    if (isGPSValid()) {
        String lat = String(gps.location.lat(), 7); // Latitud
        String lon = String(gps.location.lng(), 7); // Longitud
        String alt = String(gps.altitude.meters()); // En metres
        String sat = String(gps.satellites.value()); // Número de satèl·lits

        String redeableTime = getReadableTime(gps.time.second(), gps.time.minute(), gps.time.hour());

        SerialBT.print("( " + redeableTime + " ) GPS: ");
        SerialBT.print(getGPS(gps.location.lat(), gps.location.lng(), gps.altitude.meters()));
        SerialBT.println(", N. SAT: " + sat);
    }
    else {
        SerialBT.println("GPS not valid, try again later");
    }
}

void sendSOSMessage() {
    updateGPS();
    SOSMessage* sosMessage = new SOSMessage();
    sosMessage->altitude = gps.altitude.meters();
    sosMessage->latitude = gps.location.lat();
    sosMessage->longitude = gps.location.lng();
    ((String) contactService.myName).toCharArray(sosMessage->name, MAX_NAME_LENGTH);

    //Set the routing table list that is being used and cannot be accessed (Remember to release use after usage)
    LM_LinkedList<RouteNode>* routingTableList = radio.routingTableList();
    SerialBT.println("---- Routing Table ----");
    routingTableList->setInUse();


    for (int i = 0; i < radio.routingTableSize(); i++) {
        RouteNode* rNode = (*routingTableList)[i];
        radio.sendReliable(rNode->networkNode.address, sosMessage, 1);
    }

    //Release routing table list usage.
    routingTableList->releaseInUse();

    delete sosMessage;
}

#pragma endregion

#pragma region Contacts

void printContacts() {
    SerialBT.print(contactService.getContactsString());
}

void changeName(String name) {
    contactService.changeName(name);
    //TODO: Notify contacts that the name has been changed
}

#pragma endregion

#pragma region SerialBT

void printHelp() {
    SerialBT.println("---- Available commands ----");
    SerialBT.println("help");
    SerialBT.println("get location");
    SerialBT.println("search contacts");
    SerialBT.println("print contacts");
    SerialBT.println("change name");
    SerialBT.println("chat");
    SerialBT.println("print RT");
    SerialBT.println("---- --------- -------- ----");
}

void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
    if (event == ESP_SPP_SRV_OPEN_EVT and SerialBT.hasClient()) {
        Screen.changeLineThree("BT client connected");
        printHelp();
    }
    else if (event == ESP_SPP_CLOSE_EVT && !SerialBT.hasClient()) {
        Screen.changeLineThree("BT client disconnected");
    }
}


void initializeBluetooth() {
    SerialBT.register_callback(callback);
    if (!SerialBT.begin((String) radio.getLocalAddress())) {
        Log.errorln("BT init error");
    }
}

uint32_t state = 0;
String chatName = "";
uint16_t chatAddr = 0;

void bluetoothLoop() {
    while (SerialBT.available()) {
        String message = SerialBT.readStringUntil('\n');
        if (!message.isEmpty() || message != String('\n')) {
            message.remove(message.length() - 1, 2);
            if (message.indexOf("Back") != -1 || message.indexOf("back") != -1) {
                SerialBT.println("Going back");
                state = 0;
                continue;
            }

            switch (state) {
                case 0:
                    Serial.println("BT: " + message);

                    vTaskDelay(50);
                    if (message.indexOf("help") != -1) printHelp();
                    else if (message.indexOf("get location") != -1) printCurrentLocation();
                    else if (message.indexOf("search contacts") != -1) searchContacts();
                    else if (message.indexOf("print contacts") != -1) printContacts();
                    else if (message.indexOf("change name") != -1) {
                        SerialBT.println("Please write your new name. Write back to cancel");
                        state = 1;
                    }
                    else if (message.indexOf("chat") != -1) {
                        printContacts();
                        SerialBT.println("Write name to chat. Write back to go back");
                        state = 2;
                    }
                    else if (message.indexOf("print RT") != -1) printRoutingTable();
                    else if (message.indexOf("clear") != -1) clearDisplay();
                    else {
                        SerialBT.println("Command not found");
                    }
                    break;
                case 1:
                    if (message.length() <= MAX_NAME_LENGTH) {

                        changeName(message);
                        SerialBT.println("Hello " + message + "!");
                        state = 0;
                    }
                    else {
                        SerialBT.println("MAX NAME LENGTH is " + String(MAX_NAME_LENGTH));
                    }
                    break;
                case 2:
                    if (message.length() <= MAX_NAME_LENGTH) {
                        chatAddr = contactService.getAddrContact(message);
                        if (chatAddr == 0) {
                            SerialBT.println("Name not found in the contact list, here is your options:");
                            printContacts();
                            continue;
                        }

                        chatName = message;
                        SerialBT.println("Starting to chat to " + message + "!");
                        state = 3;
                    }
                    else {
                        SerialBT.println("MAX NAME LENGTH is " + String(MAX_NAME_LENGTH));
                    }
                    break;

                case 3:
                    sendMessage(chatAddr, message);
            }
        }
    }
}

#pragma endregion

void setup() {
    //initialize Serial Monitor
    Serial.begin(115200);
    Screen.initDisplay();

    initializeLoraMesher();

    initializeBluetooth();

    initGPS();

    pinMode(BOARD_LED, OUTPUT); //setup pin as output for indicator LED
    led_Flash(2, 100);

    vTaskDelay(100);
}

uint32_t millisRT = 0;

void loop() {
    bluetoothLoop();
    Screen.drawDisplay();

    vTaskDelay(10 / portTICK_PERIOD_MS);
}