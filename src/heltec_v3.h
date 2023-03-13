// clang-format off
// upload_speed 921600
// board heltec_wifi_lora_32_V3
#pragma once

// Pins for LORA chip SPI interface come from board file, we need some
// additional definitions for LMIC
#define LORA_IRQ 14
#define LORA_IO1 13
#define LORA_CS SS
#define LORA_SCK SCK
#define LORA_MISO MISO
#define LORA_MOSI MOSI
#define LORA_RST 12