#pragma once

// Board pins. The board is selected in platformio.ini.
#if defined(T_BEAM)
#include "boards/tbeam.h"
#else
#error "Unsupported board: define T_BEAM"
#endif

// Features
#define LORA_ENABLED
#define WIFI_ENABLED
#define MQTT_ENABLED
#define MQTT_MON_ENABLED
#define DISPLAY_ENABLED

// Site settings (WiFi credentials, MQTT broker, mesh manager). They are kept out of git: copy
// config_local.example.h to config_local.h and fill it in. Empty values disable the feature.
#if __has_include("config_local.h")
#include "config_local.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef MQTT_SERVER
#define MQTT_SERVER ""
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_USERNAME
#define MQTT_USERNAME ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif

// Address of the node that runs as LoRaMesher network manager; every other node joins it.
#ifndef LORA_MANAGER_ID
#define LORA_MANAGER_ID 0x006C
#endif

// WiFi
#define MAX_CONNECTION_TRY 10

// MQTT
#define MQTT_TOPIC_SUB "from-server/"
#define MQTT_TOPIC_OUT "to-server/"

// Display
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define DISPLAY_ADDRESS 0x3C
// Time the display stays on after boot or after a short press of the power button
#define DISPLAY_AWAKE_MS 60000

// Monitor: routing table report to MQTT
#define MON_SENDING_EVERY 300000  // ms
// Define to report all valid routes (direct + multi-hop); by default only direct neighbours.
// #define MON_REPORT_ALL_ROUTES

// LoRa RF parameters
#define LORA_FREQUENCY 869.525F
#define LORA_SPREADING_FACTOR 9U
#define LORA_BANDWIDTH 125.0
#define LORA_CODING_RATE 7U
#define LORA_POWER 17
#define LORA_SYNC_WORD 20U  // Network identifier (0-255)
#define LORA_CRC true
#define LORA_PREAMBLE_LENGTH 8U
#define LORA_DUTY_CYCLE 1.0f
#define LORA_MAX_PACKET_SIZE 255
// Max message bytes handed to LoRaMesher: packet size minus the 10-byte LoRaMesher DATA
// overhead (6-byte BaseHeader + 4-byte DataHeader).
#define MAX_MSG_SIZE 245
#define LORA_MIN_SLEEP_FRACTION 0
// Data slots each node requests at join. More slots raise per-node throughput at the cost of
// a longer superframe; the sum across nodes is capped by LORA_MAX_DATA_SLOTS.
#define LORA_DEFAULT_DATA_SLOTS 1
// Maximum number of nodes admitted to the network.
#define LORA_MAX_NETWORK_NODES 50
// Total data-slot pool shared by all nodes.
#define LORA_MAX_DATA_SLOTS 50
