// Pins and peripherals of the TTGO T-Beam v1.0/v1.1 (AXP192) and v1.2 (AXP2101).
#pragma once

// I2C bus shared by the PMU, the OLED display and external sensors
#define I2C_SDA SDA
#define I2C_SCL SCL

// OLED display (SSD1306), on the shared I2C bus
#define DISPLAY_RST -1

// LoRa radio (SX1276)
#define LORA_RADIO_TYPE loramesher::RadioType::kSx1276
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23
#define LORA_IRQ 26
#define LORA_IO1 33

// User LED, lit when the pin is low
#define LED_PIN 4
#define LED_ON LOW
#define LED_OFF HIGH

// Power management unit (AXP192 or AXP2101, detected at runtime)
#define HAS_PMU
#define PMU_IRQ 35
