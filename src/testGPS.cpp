// /*
//   This sample sketch demonstrates the normal use of a TinyGPS++ (TinyGPSPlus) object.
//   Base on TinyGPSPlus //https://github.com/mikalhart/TinyGPSPlus
// */

// #include "testGPS/LoRaBoards.h"
// #include <TinyGPS++.h>

// TinyGPSPlus gps;

// void displayInfo();

// void setup() {
//     setupBoards();

//     // When the power is turned on, a delay is required.
//     delay(1500);

//     Serial.println(F("DeviceExample.ino"));
//     Serial.println(F("A simple demonstration of TinyGPS++ with an attached GPS module"));
//     Serial.print(F("Testing TinyGPS++ library v. "));
//     Serial.println(TinyGPSPlus::libraryVersion());
//     Serial.println(F("by Mikal Hart"));
//     Serial.println();
// }

// void loop() {
//     // This sketch displays information every time a new sentence is correctly encoded.
//     while (SerialGPS.available() > 0)
//         if (gps.encode(SerialGPS.read()))
//             displayInfo();

//     if (millis() > 15000 && gps.charsProcessed() < 10) {
//         Serial.println(F("No GPS detected: check wiring."));
//         delay(15000);
//     }

//     delay(1000);
// }

// void displayInfo() {
//     Serial.print(F("Location: "));
//     if (gps.location.isValid()) {
//         Serial.print(gps.location.lat(), 6);
//         Serial.print(F(","));
//         Serial.print(gps.location.lng(), 6);
//     }
//     else {
//         Serial.print(F("INVALID"));
//     }


//     if (gps.satellites.isValid()) {
//         Serial.print(F("  Satellites: "));
//         Serial.print(gps.satellites.value());
//     }

//     Serial.print(F("  Date/Time: "));
//     if (gps.date.isValid()) {
//         Serial.print(gps.date.month());
//         Serial.print(F("/"));
//         Serial.print(gps.date.day());
//         Serial.print(F("/"));
//         Serial.print(gps.date.year());
//     }
//     else {
//         Serial.print(F("INVALID"));
//     }

//     Serial.print(F(" "));
//     if (gps.time.isValid()) {
//         if (gps.time.hour() < 10) Serial.print(F("0"));
//         Serial.print(gps.time.hour());
//         Serial.print(F(":"));
//         if (gps.time.minute() < 10) Serial.print(F("0"));
//         Serial.print(gps.time.minute());
//         Serial.print(F(":"));
//         if (gps.time.second() < 10) Serial.print(F("0"));
//         Serial.print(gps.time.second());
//         Serial.print(F("."));
//         if (gps.time.centisecond() < 10) Serial.print(F("0"));
//         Serial.print(gps.time.centisecond());
//     }
//     else {
//         Serial.print(F("INVALID"));
//     }

//     Serial.println();
// }