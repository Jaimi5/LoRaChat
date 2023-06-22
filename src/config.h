#pragma once

// #define BLUETOOTH_ENABLED

//WiFi Configuration
#define MAX_CONNECTION_TRY 10

// WiFi credentials
#define WIFI_SSID "Fibracat_21123"
#define WIFI_PASSWORD "85392c7e38"

// MQTT configuration
#define MQTT_SERVER "192.168.1.11"
#define MQTT_PORT 1883
#define MQTT_USERNAME "admin"
#define MQTT_PASSWORD "public"
#define MQTT_TOPIC_SUB "from-server/#"
#define MQTT_TOPIC_OUT "to-server/"
#define MQTT_MAX_PACKET_SIZE 512 // 128, 256 or 512
#define MQTT_MAX_QUEUE_SIZE 10
#define MQTT_STILL_CONNECTED_INTERVAL 30000 // 30 seconds

//Logging Configuration
// #define DISABLE_LOGGING

// Define default update send interval in ms
#define TEMP_UPDATE_DELAY 60000 // 60 seconds
#define DHT22_UPDATE_DELAY 60000 // 60 seconds

// Sensor configuration
#define ONE_WIRE_BUS 2

// Led configuration
// #define LED_ENABLED
#define LED LED_BUILTIN //4
#define LED_ON      LOW
#define LED_OFF     HIGH

// Display configuration
#ifdef OLED_RST
#define RST_OLED OLED_RST
#else
#define RST_OLED -1
#endif

#ifdef OLED_SDA
#define SDA_OLED OLED_SDA
#else
#define SDA_OLED SDA
#endif

#ifdef OLED_SCL
#define SCL_OLED OLED_SCL
#else
#define SCL_OLED SCL
#endif

// Simulation Configuration
#define SIMULATION_ENABLED
// The address of the device that will connect at the beginning of the simulation
#define WIFI_ADDR_CONNECTED 0x8C20


#define PACKET_COUNT 100
#define PACKET_DELAY 120000
#define PACKET_SIZE 50
#define UPLOAD_PAYLOAD 0
#define LOG_MESHER 0

// If defined, there only be one sender
#define ONE_SENDER 20068