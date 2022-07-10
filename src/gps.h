// #pragma once

// #include "Arduino.h"

// #include <HardwareSerial.h>
// #include <TinyGPS++.h>
// #include <math.h>
// #include <Wire.h>
// #include <axp20x.h>


// #define GPS_ACCURACY 7
// #define PERIOD 2000

// struct GPSData {
//   double latitude;
//   double longitude;
//   double altitude;
// };

// unsigned long localGPSFixMillis;
// unsigned long receivedGPSFixMillis;
// unsigned long lastKnownHaversineDistanceMillis;

// long lastSendTime = 0;
// int interval = PERIOD;
// long haversine_distance = 0.0;

// GPSData localGPSData = {0.0, 0.0, 0.0};
// GPSData lastKnownLocalGPSData = {0.0, 0.0, 0.0};

// void initGPS();
// bool isGPSAvailable();
// bool isGPSsameAsLastKnown(struct GPSData*, struct GPSData*);
// bool doesBothPeerHaveGPSAtSimilarTime(long, long);
// bool isGPSValid(struct GPSData*);
// void getGPSData(struct GPSData*);
// double distance(double lat1, double lng1, double lat2, double lng2);