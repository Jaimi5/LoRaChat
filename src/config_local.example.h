// Site settings for this deployment. Copy to config_local.h (gitignored) and fill in.
// WiFi credentials are only a default: credentials stored in NVS (/addSSID, /addPassword) win.
#pragma once

#define WIFI_SSID ""
#define WIFI_PASSWORD ""

#define MQTT_SERVER ""
#define MQTT_PORT 1883
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""

// Address of the node that runs as LoRaMesher network manager
#define LORA_MANAGER_ID 0x0000

// Folder with manifest.bin and firmware.bin. Default: http://loramesher.com/fw/<env>/.
// For the bench, the PC that runs scripts/ota_tools/fw_server.py, e.g. "http://192.168.1.50:8070/"
// #define OTA_SERVER_URL ""
