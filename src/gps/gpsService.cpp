#include "gpsService.h"

HardwareSerial GPS(1);

void GPSService::initGPS() {

    Wire.begin((int) SDA, (int) SCL);

#if defined(T_BEAM_V10)
    if (!axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
        Serial.println("AXP192 Begin PASS");
    }
    else {
        Serial.println("AXP192 Begin FAIL");
    }
    axp.setPowerOutPut(AXP192_LDO3, AXP202_ON); // GPS main power
    axp.setPowerOutPut(AXP192_LDO2, AXP202_ON); // provides power to GPS backup battery
    axp.setPowerOutPut(AXP192_LDO3, AXP202_ON);
    axp.setPowerOutPut(AXP192_DCDC2, AXP202_ON);
    axp.setPowerOutPut(AXP192_EXTEN, AXP202_ON);
    axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON); // enables power to ESP32 on T-beam
    axp.setPowerOutPut(AXP192_DCDC3, AXP202_ON); // I foresee similar benefit for restting T-watch 
    // where ESP32 is on DCDC3 but remember to change I2C pins and GPS pins!
#endif 
    GPS.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.println("All comms started");
    delay(100);

    do {
        if (myGPS.begin(GPS)) {
            Serial.println("Connected to GPS");
            myGPS.setUART1Output(COM_TYPE_NMEA); //Set the UART port to output NMEA only
            myGPS.saveConfiguration(); //Save the current settings to flash and BBR
            Serial.println("GPS serial connected, output set to NMEA");
            myGPS.disableNMEAMessage(UBX_NMEA_GLL, COM_PORT_UART1);
            myGPS.disableNMEAMessage(UBX_NMEA_GSA, COM_PORT_UART1);
            myGPS.disableNMEAMessage(UBX_NMEA_GSV, COM_PORT_UART1);
            myGPS.disableNMEAMessage(UBX_NMEA_VTG, COM_PORT_UART1);
            myGPS.enableNMEAMessage(UBX_NMEA_RMC, COM_PORT_UART1);
            myGPS.enableNMEAMessage(UBX_NMEA_GGA, COM_PORT_UART1);
            myGPS.saveConfiguration(); //Save the current settings to flash and BBR
            Serial.println("Enabled/disabled NMEA sentences");
            break;
        }
        delay(1000);
    } while (1);

    createGPSTask();

    Serial.println("GPS Initialized");
}

/**
 * @brief Create a GPS Task
 *
 */
void GPSService::createGPSTask() {
    int res = xTaskCreate(
        GPSLoop,
        "GPS Task",
        4096,
        (void*) 1,
        2,
        &gps_TaskHandle);
    if (res != pdPASS) {
        Log.errorln(F("GPS task handle error: %d"), res);
    }
}

void GPSService::GPSLoop(void*) {
    GPSService& gpsService = GPSService::getInstance();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        gpsService.updateGPS();
        String GPSstring = gpsService.getGPSString();
        Serial.println(GPSstring);
    }
}

void GPSService::processReceivedMessage(messagePort port, DataMessage* message) {
    switch (message->type) {
        case GPSMessageType::reqGPS:
            gpsResponse(port, message);
            break;
        default:
            break;
    }
}


String GPSService::gpsResponse(messagePort port, DataMessage* message) {
    DataMessage* msg = (DataMessage*) getGPSMessageResponse(message);
    MessageManager::getInstance().sendMessage(port, msg);

    delete msg;

    return "GPS response sent";
}


GPSMessageResponse* GPSService::getGPSMessageResponse(DataMessage* message) {
    getGPSUpdatedWait();

    GPSMessageResponse* response = new GPSMessageResponse();

    response->messageSize = sizeof(GPSMessageResponse) - sizeof(DataMessageGeneric);

    response->appPortDst = message->appPortSrc;
    response->appPortSrc = appPort::GPSApp;
    response->addrDst = message->addrSrc;
    response->addrSrc = message->addrDst;
    response->messageId = message->messageId;
    response->type = GPSMessageType::getGPS;

    response->latitude = gps.location.lat();
    response->longitude = gps.location.lng();
    response->altitude = gps.altitude.meters();
    response->satellites = gps.satellites.value();
    response->second = gps.time.second();
    response->minute = gps.time.minute();
    response->hour = gps.time.hour();
    response->day = gps.date.day();
    response->month = gps.date.month();
    response->year = gps.date.year();

    return response;
}

bool GPSService::isGPSValid() {
    return !(gps.location.lat() == 0 && gps.location.lng() == 0);
}

void GPSService::smartDelay(unsigned long ms) {
    unsigned long start = millis();
    do {
        while (GPS.available()) {
            gps.encode(GPS.read());
        }
    } while (millis() - start < ms);
}

void GPSService::notifyUpdate() {
    xTaskNotifyGive(gps_TaskHandle);
}

void GPSService::updateGPS() {
    smartDelay(400);
    if (isGPSValid())
        previousValidGPS = gps;
}

String GPSService::getGPSString() {
    if (isGPSValid()) {
        String lat = String(gps.location.lat(), 7); // Latitude
        String lng = String(gps.location.lng(), 7); // Longitude
        String alt = String(gps.altitude.meters()); // Altitude in meters
        String sat = String(gps.satellites.value()); // Number of satellites

        String readableTime = GPSHelper::getReadableTime(gps.time.second(), gps.time.minute(), gps.time.hour());

        String readableDate = GPSHelper::getReadableDate(gps.date.day(), gps.date.month(), gps.date.year());

        return "( " + readableDate + " - " + readableTime + " ) GPS: "
            + "Lat: " + lat
            + " Lon: " + lng
            + " Alt: " + alt
            + " N. SAT: " + sat;
    }
    else {
        return String("GPS not valid, try again later");
    }
}

bool GPSService::isGPSValid(TinyGPSPlus* localGPSData) {
    return !(localGPSData->location.lat() == 0 && localGPSData->location.lng() == 0);
}

void GPSService::getGPSData(TinyGPSPlus* ll) {
    gps.encode(GPS.read());
    if (gps.location.isUpdated()) {
        ll->location = gps.location;
        ll->altitude = gps.altitude;
        ll->satellites = gps.satellites;
        ll->time = gps.time;
        ll->date = gps.date;
    }
}

double GPSService::distanceBetween(double lat1, double lng1, double lat2, double lng2) {
    return gps.distanceBetween(lat1, lng1, lat2, lng2);
}

String GPSService::getGPSUpdatedWait(uint8_t maxTries) {
    notifyUpdate();
    vTaskDelay(500 / portTICK_PERIOD_MS);
    while (!isGPSValid() && maxTries > 0) {
        notifyUpdate();
        vTaskDelay(500 / portTICK_PERIOD_MS);
        maxTries--;
    }

    return getGPSString();
}

