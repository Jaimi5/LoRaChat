#pragma once

//If the device has a GPS module
//#define GPS_ENABLED
//#define BLUETOOTH_ENABLED

//Using LILYGO TTGO T-BEAM v1.1 
#define BOARD_LED   LED_BUILTIN
#define LED_ON      LOW
#define LED_OFF     HIGH

//LoRaChat Configuration
#define LORACHAT_ENABLED
#define MAX_NAME_LENGTH 10
#define MAX_MESSAGE_LENGTH 100
#define MAX_PREVIOUS_MESSAGES 10

//WiFi Configuration
#define MAX_CONNECTION_TRY 15
#define SERVER_CONNECTION_TIMEOUT 1000 //ms
#define SERVER_PORT 8080
#define SERVER_URL "http://192.168.1.38:58394"

//Logging Configuration
// #define DISABLE_LOGGING
