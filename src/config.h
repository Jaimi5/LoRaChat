#pragma once

//If the device has a GPS module
#define GPS_ENABLED

//Using LILYGO TTGO T-BEAM v1.1 
#define BOARD_LED   4
#define LED_ON      LOW
#define LED_OFF     HIGH

//Contact Configuration
#define MAX_NAME_LENGTH 10
#define MAX_MESSAGE_LENGTH 100
#define MAX_PREVIOUS_MESSAGES 10

//WiFi Configuration
#define MAX_CONNECTION_TRY 20
#define SERVER_CONNECTION_TIMEOUT 1000 //ms

//Logging Configuration
// #define DISABLE_LOGGING