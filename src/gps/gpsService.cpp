#include "gpsService.h"

static const char* GPS_TAG = "GPSService";


#if defined(T_BEAM_V10) || defined(T_BEAM_LORA_32)
HardwareSerial GPS(1);
#elif defined(NAYAD_V1) || defined(NAYAD_V1R2)
SoftwareSerial GPS(GPS_RX, GPS_TX);
#endif

void GPSService::initGPS() {
#if defined(T_BEAM_V10)
    if (!axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
        ESP_LOGV(GPS_TAG, "AXP192 Begin PASS");
    }
    else {
        ESP_LOGV(GPS_TAG, "AXP192 Begin FAIL");
    }
    axp.setPowerOutPut(AXP192_LDO3, AXP202_ON); // GPS main power
    axp.setPowerOutPut(AXP192_LDO2, AXP202_ON); // provides power to GPS backup battery
    axp.setPowerOutPut(AXP192_LDO3, AXP202_ON);
    axp.setPowerOutPut(AXP192_DCDC2, AXP202_ON);
    axp.setPowerOutPut(AXP192_EXTEN, AXP202_ON);
    axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON); // enables power to ESP32 on T-beam
    axp.setPowerOutPut(AXP192_DCDC3, AXP202_ON); // I foresee similar benefit for restting T-watch 
    // where ESP32 is on DCDC3 but remember to change I2C pins and GPS pins!
    GPS.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
    ESP_LOGV(GPS_TAG, "All comms started");
    delay(100);

    do {
        if (myGPS.begin(GPS)) {
            ESP_LOGV(GPS_TAG, "Connected to GPS");
            myGPS.setUART1Output(COM_TYPE_NMEA); //Set the UART port to output NMEA only
            myGPS.saveConfiguration(); //Save the current settings to flash and BBR
            ESP_LOGV(GPS_TAG, "GPS serial connected, output set to NMEA");
            myGPS.disableNMEAMessage(UBX_NMEA_GLL, COM_PORT_UART1);
            myGPS.disableNMEAMessage(UBX_NMEA_GSA, COM_PORT_UART1);
            myGPS.disableNMEAMessage(UBX_NMEA_GSV, COM_PORT_UART1);
            myGPS.disableNMEAMessage(UBX_NMEA_VTG, COM_PORT_UART1);
            myGPS.enableNMEAMessage(UBX_NMEA_RMC, COM_PORT_UART1);
            myGPS.enableNMEAMessage(UBX_NMEA_GGA, COM_PORT_UART1);
            myGPS.saveConfiguration(); //Save the current settings to flash and BBR
            ESP_LOGV(GPS_TAG, "Enabled/disabled NMEA sentences");
            break;
        }
        delay(1000);
    } while (1);
#else
    GPS.begin(GPS_BAUD);

#endif
    // createGPSTask();

    ESP_LOGV(GPS_TAG, "GPS Initialized");
}

/**
 * @brief Create a GPS Task
 *
 */
void GPSService::createGPSTask() {
    int res = xTaskCreate(
        GPSLoop,
        "GPS Task",
        2048,
        (void*) 1,
        2,
        &gps_TaskHandle);
    if (res != pdPASS) {
        ESP_LOGE(GPS_TAG, "GPS task handle error: %d", res);
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
    GPSMessageGeneric* gpsMessage = (GPSMessageGeneric*) message;

    switch (gpsMessage->type) {
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

GPSMessage GPSService::getGPSMessage() {
    getGPSUpdatedWait();

    GPSMessage gpsMessage;

    gpsMessage.latitude = gps.location.lat();
    gpsMessage.longitude = gps.location.lng();
    gpsMessage.altitude = gps.altitude.meters();
    gpsMessage.satellites = gps.satellites.value();
    gpsMessage.second = gps.time.second();
    gpsMessage.minute = gps.time.minute();
    gpsMessage.hour = gps.time.hour();
    gpsMessage.day = gps.date.day();
    gpsMessage.month = gps.date.month();
    gpsMessage.year = gps.date.year();

    return gpsMessage;
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

    response->gps = getGPSMessage();

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

        String readableTime = TimeHelper::getReadableTime(gps.time.second(), gps.time.minute(), gps.time.hour());

        String readableDate = TimeHelper::getReadableDate(gps.date.day(), gps.date.month(), gps.date.year());

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
    updateGPS();
    vTaskDelay(500 / portTICK_PERIOD_MS);
    while (!isGPSValid() && maxTries > 0) {
        updateGPS();
        vTaskDelay(500 / portTICK_PERIOD_MS);
        maxTries--;
    }

    return getGPSString();
}

