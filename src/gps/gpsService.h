#pragma once

#include "Arduino.h"

#include "gpsHelper.h"

#include "ArduinoLog.h"

//GPS libraries
#include <SPI.h>
#include <axp20x.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

#define T_BEAM_V10

#if defined(T_BEAM_V10)
#include <SparkFun_Ublox_Arduino_Library.h> //http://librarymanager/All#SparkFun_Ublox_GPS
#define GPS_RX_PIN 34
#define GPS_TX_PIN 12
#endif

#define GPS_DELAY 2000 // 2 seconds
#define R 6371
#define TO_RAD (3.1415926536 / 180)

class GPSService {

public:

    /**
     * @brief Construct a new GPSService object
     *
     */
    static GPSService& getInstance() {
        static GPSService instance;
        return instance;
    }

    /**
     * @brief Init GPS
     *
     */
    void initGPS();

    /**
     * @brief Is GPS Available
     *
     * @return true If is available
     * @return false If is not available
     */
    bool isGPSAvailable();

    /**
     * @brief Is GPS valid
     *
     * @return true If is valid
     * @return false If is not valid
     */
    bool isGPSValid();

    /**
     * @brief Is GPS valid
     *
     * @param gpsData gpsData
     * @return true If is valid
     * @return false If is not valid
     */
    bool isGPSValid(TinyGPSPlus* gpsData);

    /**
     * @brief Get the GPS Data object
     *
     * @param gpsData GPS Data to be updated
     */
    void getGPSData(TinyGPSPlus* gpsData);

    /**
     * @brief Get the distance between two GPS points
     *
     * @param lat1 Latitude 1
     * @param lng1 Longitude 1
     * @param lat2 Latitude 2
     * @param lng2 Longitude 2
     * @return double Distance in metres
     */
    double distanceBetween(double lat1, double lng1, double lat2, double lng2);

    /**
     * @brief Get the GPS Data string
     *
     * @return String
     */
    String getGPSString();

    /**
     * @brief Get the GPS Data string
     *
     * @param gpsData GPS Data
     * @return String
     */
    String getGPSString(TinyGPSPlus* gpsData);

    /**
     * @brief Notifies GPS task and updates GPS data
     *
     */
    void notifyUpdate();

    String getGPSUpdatedWait(uint8_t maxTries = 10);

private:

    GPSService();

    AXP20X_Class axp;

    TaskHandle_t gps_TaskHandle = NULL;

    TinyGPSPlus gps;

    TinyGPSPlus previousValidGPS;

    SFE_UBLOX_GPS myGPS;

    void createGPSTask();

    static void GPSLoop(void*);

    void smartDelay(unsigned long ms);

    void updateGPS();
};