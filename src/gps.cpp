// #include "gps.h"

// #define R 6371
// #define TO_RAD (3.1415926536 / 180)

// #define GPS_SERIAL_NUM 1
// #define GPS_RX_PIN 34
// #define GPS_TX_PIN 12

// #define I2C_SDA         21
// #define I2C_SCL         22

// HardwareSerial GPSSerial(GPS_SERIAL_NUM);
// TinyGPSPlus gps;
// AXP20X_Class axp;

// void initGPS() {
//     Wire.begin(I2C_SDA, I2C_SCL);

//     if (!axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
//         Serial.println("AXP192 Begin PASS");
//     }
//     else {
//         Serial.println("AXP192 Begin FAIL");
//     }

//     axp.setPowerOutPut(AXP192_LDO3, AXP202_ON); // GPS main power
//     axp.setPowerOutPut(AXP192_LDO2, AXP202_ON); // provides power to GPS backup battery
//     axp.setPowerOutPut(AXP192_LDO3, AXP202_ON);
//     axp.setPowerOutPut(AXP192_DCDC2, AXP202_ON);
//     axp.setPowerOutPut(AXP192_EXTEN, AXP202_ON);
//     axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON); // enables power to ESP32 on T-beam
//     axp.setPowerOutPut(AXP192_DCDC3, AXP202_ON);

//     GPSSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
// }

// bool isGPSAvailable() {
//     if (GPSSerial.available() > 0) {
//         return true;
//     }

//     return false;
// }

// bool isGPSValid(GPSData* localGPSData) {
//     if (localGPSData->latitude == 0.000 || localGPSData->longitude == 0.000) {
//         return false;
//     }

//     return true;
// }

// void getGPSData(GPSData* ll) {
//     gps.encode(GPSSerial.read());
//     if (gps.location.isUpdated()) {
//         ll->latitude = gps.location.lat();
//         ll->longitude = gps.location.lng();
//         ll->altitude = gps.altitude.meters();
//     }
// }

// bool isGPSsameAsLastKnown(GPSData* lastKnown, GPSData* currentKnown) {
//     if (lastKnown->latitude == currentKnown->latitude) {
//         if (lastKnown->longitude == currentKnown->longitude) {
//             return true;
//         }
//     }

//     return false;
// }

// // Calcutaing haversine distance based on 2 Lat-Longs only makes sense
// // if both the Lat-Longs were received around the same time
// bool doesBothPeerHaveGPSAtSimilarTime(long localMillis, long peerMillis) {
//     long seconds = (localMillis - peerMillis) / 1000;

//     if (seconds < 0) {
//         seconds *= -1;
//     }

//     // Similar times = ~5 seconds
//     if (seconds < 5.0) {
//         return true;
//     }

//     return false;

// }
// // https://rosettacode.org/wiki/Haversine_formula#C
// // Find the distance between 2 lat-long pairs and return the distance in metres, data type double
// double distance(double lat1, double lng1, double lat2, double lng2) {
//     double dx, dy, dz;
//     lng1 -= lng2;
//     lng1 *= TO_RAD, lat1 *= TO_RAD, lat2 *= TO_RAD;

//     dz = sin(lat1) - sin(lat2);
//     dx = cos(lng1) * cos(lat1) - cos(lat2);
//     dy = sin(lng1) * cos(lat1);
//     return asin(sqrt(dx * dx + dy * dy + dz * dz) / 2) * 2 * R * 1000; // *1000 for metres
// }