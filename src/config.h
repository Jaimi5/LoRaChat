#pragma once

#define BLUETOOTH_ENABLED

//WiFi Configuration
#define MAX_CONNECTION_TRY 5

// WiFi credentials
#define WIFI_SSID "*********"
#define WIFI_PASSWORD "*********"

// MQTT configuration
#define MQTT_SERVER "192.168.0.108"
#define MQTT_PORT 1883
#define MQTT_USERNAME "admin"
#define MQTT_PASSWORD "public"
#define MQTT_TOPIC_SUB "from-server/#"
#define MQTT_TOPIC_OUT "to-server/"
#define MQTT_MAX_PACKET_SIZE 512 // 128, 256 or 512
#define MQTT_MAX_QUEUE_SIZE 10

//Logging Configuration
// #define DISABLE_LOGGING

// Define default update send interval in ms
#define TEMP_UPDATE_DELAY 60000 // 60 seconds
#define DHT22_UPDATE_DELAY 30000 // 30 seconds

// Sensor configuration
#define ONE_WIRE_BUS 2
