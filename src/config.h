#pragma once

//#define BLUETOOTH_ENABLED

//WiFi Configuration
#define MAX_CONNECTION_TRY 15

// WiFi credentials
#define WIFI_SSID "****"
#define WIFI_PASSWORD "****"

// MQTT configuration
#define MQTT_SERVER "192.168.1.16"
#define MQTT_PORT 1883
#define MQTT_USERNAME "admin"
#define MQTT_PASSWORD "public"
#define MQTT_TOPIC_SUB "inTopic"
#define MQTT_TOPIC_OUT "outTopic"

//Logging Configuration
// #define DISABLE_LOGGING

// Define default update send interval in ms
#define TEMP_UPDATE_DELAY 60000 // 60 seconds

// Sensor configuration
#define ONE_WIRE_BUS 2
