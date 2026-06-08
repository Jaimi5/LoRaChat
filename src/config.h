#pragma once

// USE_LORAMESHER_V2 is set via platformio.ini build_flags (in base_v2 / *-v2 envs)
// Defined: uses LoRaMesher v1.0.0 (Builder pattern, callbacks)
// Not defined: uses LoRaMesher v0.0.11 (singleton, task-based)

// Choose the device, choose it directly in the platformio.ini file
// #define T_BEAM_V10 // ttgo-t-beam
// #define T_BEAM_LORA_32 // ttgo-lora32-v1
// #define NAYAD_V1
// #define NAYAD_V1R2
// #define MAKERFABS_SENSELORA_MOISTURE
#if defined USE_LORAMESHER_V2
#define LORAMESHER_VERSION "v1.0.0"
#else
#define LORAMESHER_VERSION "v0.0.8"
#endif

#if defined(NAYAD_V1) || defined(NAYAD_V1R2)
// #define GPS_ENABLED
// #define DISPLAY_ENABLED
// #define BATTERY_ENABLED
#define LED_ENABLED
// #define SENSORS_ENABLED
// #define METADATA_ENABLED
#define WIFI_ENABLED
#define MQTT_ENABLED
#define MQTT_MON_ENABLED
// #define BLUETOOTH_ENABLED
#define LORA_ENABLED
#define SIMULATION_ENABLED
#elif defined(T_BEAM_V10) || defined(T_BEAM_V12)
#define DISPLAY_ENABLED
// #define LED_ENABLED
#define LORA_ENABLED
#define WIFI_ENABLED
#define MQTT_ENABLED
#define MQTT_MON_ENABLED
// #define BLUETOOTH_ENABLED
// #define GPS_ENABLED
// #define SIMULATION_ENABLED
// #define NO_SENSOR_DATA // If the sensors are not connected
#elif defined(T_BEAM_LORA_32)
#define DISPLAY_ENABLED
// #define LED_ENABLED
#define LORA_ENABLED
#define SIMULATION_ENABLED
#define WIFI_ENABLED
#define MQTT_ENABLED
// #define BLUETOOTH_ENABLED
#elif defined(MAKERFABS_SENSELORA_MOISTURE)
#define LORA_ENABLED
#define WIFI_ENABLED
#define MQTT_ENABLED
#endif

// Configuration

// Display Configuration
#ifdef NAYAD_V1
#define I2C_SDA 02
#define I2C_SCL 04
#elif defined(NAYAD_V1R2) || defined(T_BEAM_V10) || defined(T_BEAM_LORA_32) || defined(T_BEAM_V12)
#define I2C_SDA SDA
#define I2C_SCL SCL
#else
#warning "I2C_SDA and I2C_SCL not defined"
#define I2C_SDA 0
#define I2C_SCL 0
#endif


// If the device has a GPS module
#if defined(T_BEAM_V10) || defined(T_BEAM_V12)
#define GPS_TX 12
#define GPS_RX 34
#elif defined(NAYAD_V1)
#define GPS_RX 25
#define GPS_TX 35
#elif defined(NAYAD_V1R2)
#define GPS_RX 4
#define GPS_TX 2
#endif
#define GPS_BAUD 9600
#define UPDATE_GPS_DELAY 120000  // ms

#if defined(T_BEAM_LORA_32) && defined(GPS_ENABLED)
#warning "GPS in T_BEAM_LORA_32 is not default supported"
#endif


// Display Configuration
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define DISPLAY_ADDRESS 0x3C

#if defined(T_BEAM_LORA_32)
#define DISPLAY_SDA 4
#define DISPLAY_SCL 15
#define DISPLAY_RST 16
#elif defined(NAYAD_V1) || defined(NAYAD_V1R2) || defined(T_BEAM_V10) || defined(T_BEAM_V12)
#define DISPLAY_SDA I2C_SDA
#define DISPLAY_SCL I2C_SCL
#define DISPLAY_RST -1
#else
#warning "DISPLAY_SDA and DISPLAY_SCL not defined"
#define DISPLAY_SDA 0
#define DISPLAY_SCL 0
#define DISPLAY_RST -1
#endif


// WiFi Configuration
#define MAX_CONNECTION_TRY 10

// WiFi credentials
#define WIFI_SSID "******"
#define WIFI_PASSWORD "******"
#define WIFI_OVERRIDE_CREDENTIALS //If defined, every time the device is reset it will set the wifi credentials.

// MQTT configuration
#define MQTT_SERVER "192.168.1.26"
#define MQTT_PORT 1883
#define MQTT_USERNAME "admin"
#define MQTT_PASSWORD "public"
#define MQTT_TOPIC_SUB "from-server/"
#define MQTT_TOPIC_OUT "to-server/"
#define MQTT_MAX_PACKET_SIZE 512  // 128, 256 or 512
#define MQTT_MAX_QUEUE_SIZE 10
#define MQTT_STILL_CONNECTED_INTERVAL 300000  // In milliseconds, 0 to disable

// Sensors Configuration
#define STORED_SENSOR_DATA 10
//- Temperature Configuration
#define SOIL_SENSOR_PIN 12
#define SENSOR_SENDING_EVERY 60000  // ms
//- Metadata Configuration
#define METADATA_UPDATE_DELAY 300000  // ms

// MQTT_MON configuration
#define MON_SENDING_EVERY 300000  // ms

// Monitor reporting mode: define to report all valid routes (direct + multi-hop)
// Undefine (default) to report only direct neighbors (1-hop), matching v1 behavior
// #define MON_REPORT_ALL_ROUTES


// Battery configuration
#if defined(MAKERFABS_SENSELORA_MOISTURE)
#define BATTERY_PIN 14
#else
#define BATTERY_PIN 34
#endif
#define DEEP_SLEEP_TIME 3600  // In seconds


// Led configuration
#if defined(NAYAD_V1)
#define LED 4
#define LED_ON LOW
#define LED_OFF HIGH
#elif defined(T_BEAM_LORA_32)
#define LED 2
#define LED_ON HIGH
#define LED_OFF LOW
#elif defined(T_BEAM_V10) || defined(T_BEAM_V12)
#define LED 4
#define LED_ON LOW
#define LED_OFF HIGH
#elif defined(NAYAD_V1R2)
#define LED 13
#define LED_ON HIGH
#define LED_OFF LOW
#else
#warning "LED not defined"
#define LED 255U
#define LED_ON HIGH
#define LED_OFF LOW
#endif


// LoRa Configuration
#if defined(T_BEAM_LORA_32) || defined(T_BEAM_V10) || defined(T_BEAM_V12) || \
    defined(MAKERFABS_SENSELORA_MOISTURE)
#define LORA_MODULE_SX1276 0
#elif defined(NAYAD_V1) || defined(NAYAD_V1R2)
#define LORA_MODULE_SX1262 1
#else
#warning "LORA_MODULE not defined"
#endif

#if defined(MAKERFABS_SENSELORA_MOISTURE)
#define POWER_LORA 21
#endif


#ifndef LORA_SCK
#if defined(NAYAD_V1)
#define LORA_SCK 14
#elif defined(NAYAD_V1R2)
#define LORA_SCK 18
#elif defined(T_BEAM_LORA_32)
#define LORA_SCK 5
#elif defined(T_BEAM_V10) || defined(T_BEAM_V12)
#define LORA_SCK 5
#elif defined(MAKERFABS_SENSELORA_MOISTURE)
#define LORA_SCK 12
#else
#define LORA_SCK LORA_SCK
#endif
#endif


#ifndef LORA_MISO
#if defined(NAYAD_V1)
#define LORA_MISO 12
#elif defined(NAYAD_V1R2)
#define LORA_MISO 19
#elif defined(T_BEAM_LORA_32)
#define LORA_MISO 19
#elif defined(T_BEAM_V10) || defined(T_BEAM_V12)
#define LORA_MISO 19
#elif defined(MAKERFABS_SENSELORA_MOISTURE)
#define LORA_MISO 13
#else
#define LORA_MISO LORA_MISO
#endif
#endif


#ifndef LORA_MOSI
#if defined(NAYAD_V1)
#define LORA_MOSI 13
#elif defined(NAYAD_V1R2)
#define LORA_MOSI 23
#elif defined(T_BEAM_LORA_32)
#define LORA_MOSI 27
#elif defined(T_BEAM_V10) || defined(T_BEAM_V12)
#define LORA_MOSI 27
#elif defined(MAKERFABS_SENSELORA_MOISTURE)
#define LORA_MOSI 11
#else
#define LORA_MOSI LORA_MOSI
#endif
#endif


#ifndef LORA_CS
#if defined(NAYAD_V1) || defined(NAYAD_V1R2)
#define LORA_CS 15
#elif defined(T_BEAM_LORA_32)
#define LORA_CS 18
#elif defined(T_BEAM_V10) || defined(T_BEAM_V12)
#define LORA_CS 18
#elif defined(MAKERFABS_SENSELORA_MOISTURE)
#define LORA_CS 4
#else
#define LORA_CS 255U
#endif
#endif


#ifndef LORA_RST
#if defined(NAYAD_V1) || defined(NAYAD_V1R2)
#define LORA_RST 27
#elif defined(T_BEAM_LORA_32)
#define LORA_RST 14
#elif defined(T_BEAM_V10) || defined(T_BEAM_V12)
#define LORA_RST 23
#elif defined(MAKERFABS_SENSELORA_MOISTURE)
#define LORA_RST 5
#else
#warning "LORA_RST not defined"
#define LORA_RST 255U
#endif
#endif


#ifndef LORA_IRQ
#if defined(NAYAD_V1)
#define LORA_IRQ 26
#elif defined(NAYAD_V1R2)
#define LORA_IRQ 33
#elif defined(T_BEAM_LORA_32)
#define LORA_IRQ 26
#elif defined(T_BEAM_V10) || defined(T_BEAM_V12)
#define LORA_IRQ 26
#elif defined(MAKERFABS_SENSELORA_MOISTURE)
#define LORA_IRQ 6
#else
#define LORA_IRQ 255U
#endif
#endif


#ifndef LORA_IO1
#if defined(NAYAD_V1)
#define LORA_IO1 33
#elif defined(NAYAD_V1R2)
#define LORA_IO1 14
#elif defined(T_BEAM_LORA_32)
#define LORA_IO1 33
#elif defined(MAKERFABS_SENSELORA_MOISTURE)
#define LORA_IO1 7
#elif defined(T_BEAM_V10) || defined(T_BEAM_V12)
#define LORA_IO1 33
#else
#ifndef LORA_MODULE_SX1276
#warning "LORA_IO1 not defined"
#endif
#define LORA_IO1 255U
#endif
#endif

// Testbed: 0 makes initLoRaMesher() return early, so the node boots with no
// LoRa stack and never joins the mesh. Used by experiment runs that need a
// silent-node subset (e.g. density sweeps within the 13-node main cluster).
#define NODE_ACTIVE 1

#define LORA_MANAGER_ID 0x006C

// LoRa RF Parameters
#define LORA_FREQUENCY 869.900F
#define LORA_SPREADING_FACTOR 9U
#define LORA_BANDWIDTH 125.0
#define LORA_CODING_RATE 7U
#define LORA_POWER 17
#define LORA_SYNC_WORD 20U      // Network identifier (0-255)
#define LORA_CRC true           // Enable CRC checking
#define LORA_PREAMBLE_LENGTH 8U
#define LORA_DUTY_CYCLE 1.0f
#define LORA_MAX_PACKET_SIZE 255
// Max monitor message bytes handed to LoRaMesher = packet size minus the
// 10-byte LoRaMesher DATA overhead (6-byte BaseHeader + 4-byte DataHeader).
#define MAX_MSG_SIZE 245
#define LORA_MIN_SLEEP_FRACTION 0
// Data slots each node requests at join. More slots amortize the fixed control
// overhead (higher per-node throughput) at the cost of a longer superframe; the
// sum across nodes is capped by the data-slot pool (max_network_nodes).
#define LORA_DEFAULT_DATA_SLOTS 1
// Maximum number of nodes admitted to the network (node-count cap only).
#define LORA_MAX_NETWORK_NODES 50
// Total data-slot pool: sum of every node's data slots. Independent of the node
// cap (LoRaMesher separates the two). Raise above nodes x data_slots when
// sweeping default_data_slots so late joiners aren't starved of data slots.
#define LORA_MAX_DATA_SLOTS 50
#ifdef USE_LORAMESHER_V2
#define LORA_RADIO_TYPE loramesher::RadioType::kSx1276
#endif

// PMU configuration
#if defined(T_BEAM_V10) || defined(T_BEAM_V12)
#define HAS_PMU
#define PMU_IRQ 35
#endif


// Simulation Configuration
// The address of the device that will connect at the beginning of the simulation
#define WIFI_ADDR_CONNECTED 0x3ADF

#define PACKET_COUNT 200
#define PACKET_DELAY 120000
#define PACKET_SIZE 50
#define UPLOAD_PAYLOAD 0
#define LOG_MESHER 0

// If defined, there only be one sender
#define ONE_SENDER 0

// If defined 0 the packets will be sent unreliably
#define SEND_RELIABLE 0

// Simulator Delay Configuration (all times in milliseconds unless specified)
#define SIM_NETWORK_PROPAGATION_MULTIPLIER 5
#define SIM_INITIAL_WIFI_DELAY 30000
#define SIM_POST_START_DELAY 30000
#define SIM_UPLOAD_DELAY_CONNECTED 2000
#define SIM_UPLOAD_DELAY_DISCONNECTED 40000
#define SIM_QUEUE_CONGESTION_DELAY 20000
#define SIM_NON_SENDER_WAIT 600000
#define SIM_POST_MQTT_DELAY 1000
