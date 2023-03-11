// clang-format off
// upload_speed 921600
// board heltec_wifi_lora_32_V2

#ifndef _HELTECV2_H
#define _HELTECV2_H

// Pins for LORA chip SPI interface come from board file, we need some
// additional definitions for LMIC
#define LORA_IRQ DIO0
#define LORA_IO1 DIO1
#define LORA_IO2 DIO2
#define LORA_SCK SCK
#define LORA_MISO MISO
#define LORA_MOSI MOSI
#define LORA_RST RST_LoRa
#define LORA_CS SS

#endif