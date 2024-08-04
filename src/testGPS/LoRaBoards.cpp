// /**
//  * @file      boards.cpp
//  * @author    Lewis He (lewishe@outlook.com)
//  * @license   MIT
//  * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
//  * @date      2024-04-24
//  *
//  */

// #include "LoRaBoards.h"

// #if defined(HAS_SDCARD)
// SPIClass SDCardSPI(HSPI);
// #endif


// #if defined(ARDUINO_ARCH_STM32)
// HardwareSerial  SerialGPS(GPS_RX_PIN, GPS_TX_PIN);
// #endif

// #if defined(ARDUINO_ARCH_ESP32)
// #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
// #include "hal/gpio_hal.h"
// #endif
// #include "driver/gpio.h"
// #endif //ARDUINO_ARCH_ESP32


// DISPLAY_MODEL* u8g2 = NULL;
// static DevInfo_t  devInfo;


// #ifdef HAS_PMU
// XPowersLibInterface* PMU = NULL;
// bool     pmuInterrupt;

// static void setPmuFlag() {
//     pmuInterrupt = true;
// }
// #endif

// bool beginPower() {
// #ifdef HAS_PMU
//     if (!PMU) {
//         PMU = new XPowersAXP2101(PMU_WIRE_PORT);
//         if (!PMU->init()) {
//             Serial.println("Warning: Failed to find AXP2101 power management");
//             delete PMU;
//             PMU = NULL;
//         }
//         else {
//             Serial.println("AXP2101 PMU init succeeded, using AXP2101 PMU");
//         }
//     }

//     if (!PMU) {
//         PMU = new XPowersAXP192(PMU_WIRE_PORT);
//         if (!PMU->init()) {
//             Serial.println("Warning: Failed to find AXP192 power management");
//             delete PMU;
//             PMU = NULL;
//         }
//         else {
//             Serial.println("AXP192 PMU init succeeded, using AXP192 PMU");
//         }
//     }

//     if (!PMU) {
//         return false;
//     }

//     PMU->setChargingLedMode(XPOWERS_CHG_LED_BLINK_1HZ);

//     pinMode(PMU_IRQ, INPUT_PULLUP);
//     attachInterrupt(PMU_IRQ, setPmuFlag, FALLING);

//     if (PMU->getChipModel() == XPOWERS_AXP192) {

//         Serial.printf("AXP192 PMU init succeeded, using AXP192 PMU\n");

//         PMU->setProtectedChannel(XPOWERS_DCDC3);

//         // lora
//         PMU->setPowerChannelVoltage(XPOWERS_LDO2, 3300);
//         // gps
//         PMU->setPowerChannelVoltage(XPOWERS_LDO3, 3300);
//         // oled
//         PMU->setPowerChannelVoltage(XPOWERS_DCDC1, 3300);

//         PMU->enablePowerOutput(XPOWERS_LDO2);
//         PMU->enablePowerOutput(XPOWERS_LDO3);

//         //protected oled power source
//         PMU->setProtectedChannel(XPOWERS_DCDC1);
//         //protected esp32 power source
//         PMU->setProtectedChannel(XPOWERS_DCDC3);
//         // enable oled power
//         PMU->enablePowerOutput(XPOWERS_DCDC1);

//         //disable not use channel
//         PMU->disablePowerOutput(XPOWERS_DCDC2);

//         PMU->disableIRQ(XPOWERS_AXP192_ALL_IRQ);

//         PMU->enableIRQ(XPOWERS_AXP192_VBUS_REMOVE_IRQ |
//             XPOWERS_AXP192_VBUS_INSERT_IRQ |
//             XPOWERS_AXP192_BAT_CHG_DONE_IRQ |
//             XPOWERS_AXP192_BAT_CHG_START_IRQ |
//             XPOWERS_AXP192_BAT_REMOVE_IRQ |
//             XPOWERS_AXP192_BAT_INSERT_IRQ |
//             XPOWERS_AXP192_PKEY_SHORT_IRQ
//         );

//     }
//     else if (PMU->getChipModel() == XPOWERS_AXP2101) {
//         Serial.printf("AXP2101 PMU init succeeded, using AXP2101 PMU\n");

//         PMU->setProtectedChannel(XPOWERS_DCDC1);

//         //Unuse power channel
//         PMU->disablePowerOutput(XPOWERS_DCDC2);
//         PMU->disablePowerOutput(XPOWERS_DCDC3);
//         PMU->disablePowerOutput(XPOWERS_DCDC4);
//         PMU->disablePowerOutput(XPOWERS_DCDC5);

//         // PMU->disablePowerOutput(XPOWERS_ALDO1);
//         PMU->disablePowerOutput(XPOWERS_ALDO4);

//         PMU->disablePowerOutput(XPOWERS_BLDO1);
//         PMU->disablePowerOutput(XPOWERS_BLDO2);

//         PMU->disablePowerOutput(XPOWERS_DLDO1);
//         PMU->disablePowerOutput(XPOWERS_DLDO2);

//         PMU->enablePowerOutput(XPOWERS_VBACKUP);
//         PMU->enablePowerOutput(XPOWERS_ALDO1);
//         PMU->enablePowerOutput(XPOWERS_ALDO2);
//         PMU->enablePowerOutput(XPOWERS_ALDO3);

//         PMU->setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V4);


//         // GNSS RTC PowerVDD 3300mV
//         // PMU->setPowerChannelVoltage(XPOWERS_VBACKUP, 3300);

//         // //ESP32 VDD 3300mV
//         // // ! No need to set, automatically open , Don't close it
//         // // PMU->setPowerChannelVoltage(XPOWERS_DCDC1, 3300);
//         // // PMU->setProtectedChannel(XPOWERS_DCDC1);

//         // // LoRa VDD 3300mV
//         // PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
//         // PMU->enablePowerOutput(XPOWERS_ALDO2);

//         // //GNSS VDD 3300mV
//         // PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);
//         // PMU->enablePowerOutput(XPOWERS_ALDO3);
//     }

//     PMU->clearIrqStatus();

//     // TBeam1.1 /T-Beam S3-Core has no external TS detection,
//     // it needs to be disabled, otherwise it will cause abnormal charging
//     PMU->disableTSPinMeasure();

//     // PMU->enableSystemVoltageMeasure();
//     PMU->enableVbusVoltageMeasure();
//     PMU->enableBattVoltageMeasure();

//     Serial.printf("=========================================\n");
//     if (PMU->isChannelAvailable(XPOWERS_DCDC1)) {
//         Serial.printf("DC1  : %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_DCDC1) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_DCDC1));
//     }
//     if (PMU->isChannelAvailable(XPOWERS_DCDC2)) {
//         Serial.printf("DC2  : %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_DCDC2) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_DCDC2));
//     }
//     if (PMU->isChannelAvailable(XPOWERS_DCDC3)) {
//         Serial.printf("DC3  : %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_DCDC3) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_DCDC3));
//     }
//     if (PMU->isChannelAvailable(XPOWERS_DCDC4)) {
//         Serial.printf("DC4  : %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_DCDC4) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_DCDC4));
//     }
//     if (PMU->isChannelAvailable(XPOWERS_DCDC5)) {
//         Serial.printf("DC5  : %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_DCDC5) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_DCDC5));
//     }
//     if (PMU->isChannelAvailable(XPOWERS_LDO2)) {
//         Serial.printf("LDO2 : %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_LDO2) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_LDO2));
//     }
//     if (PMU->isChannelAvailable(XPOWERS_LDO3)) {
//         Serial.printf("LDO3 : %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_LDO3) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_LDO3));
//     }
//     if (PMU->isChannelAvailable(XPOWERS_ALDO1)) {
//         Serial.printf("ALDO1: %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_ALDO1) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_ALDO1));
//     }
//     if (PMU->isChannelAvailable(XPOWERS_ALDO2)) {
//         Serial.printf("ALDO2: %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_ALDO2) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_ALDO2));
//     }
//     if (PMU->isChannelAvailable(XPOWERS_ALDO3)) {
//         Serial.printf("ALDO3: %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_ALDO3) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_ALDO3));
//     }
//     if (PMU->isChannelAvailable(XPOWERS_ALDO4)) {
//         Serial.printf("ALDO4: %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_ALDO4) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_ALDO4));
//     }
//     if (PMU->isChannelAvailable(XPOWERS_BLDO1)) {
//         Serial.printf("BLDO1: %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_BLDO1) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_BLDO1));
//     }
//     if (PMU->isChannelAvailable(XPOWERS_BLDO2)) {
//         Serial.printf("BLDO2: %s   Voltage: %04u mV \n", PMU->isPowerChannelEnable(XPOWERS_BLDO2) ? "+" : "-", PMU->getPowerChannelVoltage(XPOWERS_BLDO2));
//     }
//     Serial.printf("=========================================\n");


//     // Set the time of pressing the button to turn off
//     PMU->setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
//     uint8_t opt = PMU->getPowerKeyPressOffTime();
//     Serial.print("PowerKeyPressOffTime:");
//     switch (opt) {
//         case XPOWERS_POWEROFF_4S: Serial.println("4 Second");
//             break;
//         case XPOWERS_POWEROFF_6S: Serial.println("6 Second");
//             break;
//         case XPOWERS_POWEROFF_8S: Serial.println("8 Second");
//             break;
//         case XPOWERS_POWEROFF_10S: Serial.println("10 Second");
//             break;
//         default:
//             break;
//     }
// #endif
//     return true;
// }

// void disablePeripherals() {


// }

// bool beginDisplay() {
//     Wire.beginTransmission(DISPLAY_ADDR);
//     if (Wire.endTransmission() == 0) {
//         Serial.printf("Find Display model at 0x%X address\n", DISPLAY_ADDR);
//         u8g2 = new DISPLAY_MODEL(U8G2_R0, U8X8_PIN_NONE);
//         u8g2->begin();
//         u8g2->clearBuffer();
//         u8g2->setFont(u8g2_font_inb19_mr);
//         u8g2->drawStr(0, 30, "LilyGo");
//         u8g2->drawHLine(2, 35, 47);
//         u8g2->drawHLine(3, 36, 47);
//         u8g2->drawVLine(45, 32, 12);
//         u8g2->drawVLine(46, 33, 12);
//         u8g2->setFont(u8g2_font_inb19_mf);
//         u8g2->drawStr(58, 60, "LoRa");
//         u8g2->sendBuffer();
//         u8g2->setFont(u8g2_font_fur11_tf);
//         delay(3000);
//         return true;
//     }

//     Serial.printf("Warning: Failed to find Display at 0x%0X address\n", DISPLAY_ADDR);
//     return false;
// }


// bool beginSDCard() {
// #ifdef SDCARD_CS
//     if (SD.begin(SDCARD_CS, SDCardSPI)) {
//         uint32_t cardSize = SD.cardSize() / (1024 * 1024);
//         Serial.print("Sd Card init succeeded, The current available capacity is ");
//         Serial.print(cardSize / 1024.0);
//         Serial.println(" GB");
//         return true;
//     }
//     else {
//         Serial.println("Warning: Failed to init Sd Card");
//     }
// #endif
//     return false;
// }

// void beginWiFi() {


// }


// void printWakeupReason() {
// #ifdef ESP32
//     Serial.print("Reset reason:");
//     esp_sleep_wakeup_cause_t wakeup_reason;
//     wakeup_reason = esp_sleep_get_wakeup_cause();
//     switch (wakeup_reason) {
//         case ESP_SLEEP_WAKEUP_UNDEFINED:
//             Serial.println(" In case of deep sleep, reset was not caused by exit from deep sleep");
//             break;
//         case ESP_SLEEP_WAKEUP_ALL:
//             break;
//         case ESP_SLEEP_WAKEUP_EXT0:
//             Serial.println("Wakeup caused by external signal using RTC_IO");
//             break;
//         case ESP_SLEEP_WAKEUP_EXT1:
//             Serial.println("Wakeup caused by external signal using RTC_CNTL");
//             break;
//         case ESP_SLEEP_WAKEUP_TIMER:
//             Serial.println("Wakeup caused by timer");
//             break;
//         case ESP_SLEEP_WAKEUP_TOUCHPAD:
//             Serial.println("Wakeup caused by touchpad");
//             break;
//         case ESP_SLEEP_WAKEUP_ULP:
//             Serial.println("Wakeup caused by ULP program");
//             break;
//         default:
//             Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
//             break;
//     }
// #endif
// }


// void getChipInfo() {
// #if defined(ARDUINO_ARCH_ESP32)

//     Serial.println("-----------------------------------");

//     printWakeupReason();

// #if defined(CONFIG_IDF_TARGET_ESP32)  ||  defined(CONFIG_IDF_TARGET_ESP32S3)

//     if (psramFound()) {
//         uint32_t psram = ESP.getPsramSize();
//         devInfo.psramSize = psram / 1024.0 / 1024.0;
//         Serial.printf("PSRAM is enable! PSRAM: %.2fMB\n", devInfo.psramSize);
//     }
//     else {
//         Serial.println("PSRAM is disable!");
//         devInfo.psramSize = 0;
//     }

// #endif

//     Serial.print("Flash:");
//     devInfo.flashSize = ESP.getFlashChipSize() / 1024.0 / 1024.0;
//     devInfo.flashSpeed = ESP.getFlashChipSpeed() / 1000 / 1000;
//     devInfo.chipModel = ESP.getChipModel();
//     devInfo.chipModelRev = ESP.getChipRevision();
//     devInfo.chipFreq = ESP.getCpuFreqMHz();

//     Serial.print(devInfo.flashSize);
//     Serial.println(" MB");
//     Serial.print("Flash speed:");
//     Serial.print(devInfo.flashSpeed);
//     Serial.println(" M");
//     Serial.print("Model:");

//     Serial.println(devInfo.chipModel);
//     Serial.print("Chip Revision:");
//     Serial.println(devInfo.chipModelRev);
//     Serial.print("Freq:");
//     Serial.print(devInfo.chipFreq);
//     Serial.println(" MHZ");
//     Serial.print("SDK Ver:");
//     Serial.println(ESP.getSdkVersion());
//     Serial.print("DATE:");
//     Serial.println(__DATE__);
//     Serial.print("TIME:");
//     Serial.println(__TIME__);

//     Serial.print("EFUSE MAC: ");
//     Serial.print(ESP.getEfuseMac(), HEX);
//     Serial.println();

//     Serial.println("-----------------------------------");

// #elif defined(ARDUINO_ARCH_STM32)
//     uint32_t uid[3];

//     uid[0] = HAL_GetUIDw0();
//     uid[1] = HAL_GetUIDw1();
//     uid[2] = HAL_GetUIDw2();
//     Serial.print("STM UID: 0X");
//     Serial.print(uid[0], HEX);
//     Serial.print(uid[1], HEX);
//     Serial.print(uid[2], HEX);
//     Serial.println();
// #endif
// }



// void setupBoards() {
//     Serial.begin(115200);

//     Serial.println("setupBoards");

//     Serial.begin(115200);

//     getChipInfo();

// #if defined(ARDUINO_ARCH_ESP32)
//     SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
// #elif defined(ARDUINO_ARCH_STM32)
//     SPI.setMISO(RADIO_MISO_PIN);
//     SPI.setMOSI(RADIO_MOSI_PIN);
//     SPI.setSCLK(RADIO_SCLK_PIN);
//     SPI.begin();
// #endif

// #ifdef HAS_SDCARD
//     SDCardSPI.begin(SDCARD_SCLK, SDCARD_MISO, SDCARD_MOSI);
// #endif

// #ifdef I2C_SDA
//     Wire.begin(I2C_SDA, I2C_SCL);
// #endif

// #ifdef I2C1_SDA
//     Wire1.begin(I2C1_SDA, I2C1_SCL);
// #endif

// #ifdef HAS_GPS
// #if defined(ARDUINO_ARCH_ESP32)
//     SerialGPS.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
// #elif defined(ARDUINO_ARCH_STM32)
//     SerialGPS.setRx(GPS_RX_PIN);
//     SerialGPS.setTx(GPS_TX_PIN);
//     SerialGPS.begin(GPS_BAUD_RATE);
// #endif // ARDUINO_ARCH_
// #endif // HAS_GPS

// #if OLED_RST
//     pinMode(OLED_RST, OUTPUT);
//     digitalWrite(OLED_RST, HIGH); delay(20);
//     digitalWrite(OLED_RST, LOW);  delay(20);
//     digitalWrite(OLED_RST, HIGH); delay(20);
// #endif

// #ifdef BOARD_LED
//     /*
//     * T-Beam LED defaults to low level as turn on,
//     * so it needs to be forced to pull up
//     * * * * */
// #if LED_ON == LOW
// #if defined(ARDUINO_ARCH_ESP32)
//     gpio_hold_dis((gpio_num_t) BOARD_LED);
// #endif //ARDUINO_ARCH_ESP32
// #endif

//     pinMode(BOARD_LED, OUTPUT);
//     digitalWrite(BOARD_LED, LED_ON);
// #endif

// #ifdef GPS_EN_PIN
//     pinMode(GPS_EN_PIN, OUTPUT);
//     digitalWrite(GPS_EN_PIN, HIGH);
// #endif

// #ifdef GPS_RST_PIN
//     pinMode(GPS_RST_PIN, OUTPUT);
//     digitalWrite(GPS_RST_PIN, HIGH);
// #endif


// #if defined(ARDUINO_ARCH_STM32)
//     SerialGPS.println("@GSR"); delay(300);
//     SerialGPS.println("@GSR"); delay(300);
//     SerialGPS.println("@GSR"); delay(300);
//     SerialGPS.println("@GSR"); delay(300);
//     SerialGPS.println("@GSR"); delay(300);
// #endif

//     beginPower();

//     beginSDCard();

//     beginDisplay();

//     beginWiFi();

//     Serial.println("init done . ");
// }


// void printResult(bool radio_online) {
//     Serial.print("Radio        : ");
//     Serial.println((radio_online) ? "+" : "-");

// #if defined(CONFIG_IDF_TARGET_ESP32)  ||  defined(CONFIG_IDF_TARGET_ESP32S3)

//     Serial.print("PSRAM        : ");
//     Serial.println((psramFound()) ? "+" : "-");

//     Serial.print("Display      : ");
//     Serial.println((u8g2) ? "+" : "-");

// #ifdef HAS_SDCARD
//     Serial.print("Sd Card      : ");
//     Serial.println((SD.cardSize() != 0) ? "+" : "-");
// #endif

// #ifdef HAS_PMU
//     Serial.print("Power        : ");
//     Serial.println((PMU) ? "+" : "-");
// #endif


//     if (u8g2) {

//         u8g2->clearBuffer();
//         u8g2->setFont(u8g2_font_NokiaLargeBold_tf);
//         uint16_t str_w = u8g2->getStrWidth(BOARD_VARIANT_NAME);
//         u8g2->drawStr((u8g2->getWidth() - str_w) / 2, 16, BOARD_VARIANT_NAME);
//         u8g2->drawHLine(5, 21, u8g2->getWidth() - 5);

//         u8g2->drawStr(0, 38, "Disp:");     u8g2->drawStr(45, 38, (u8g2) ? "+" : "-");

// #ifdef HAS_SDCARD
//         u8g2->drawStr(0, 54, "SD :");      u8g2->drawStr(45, 54, (SD.cardSize() != 0) ? "+" : "-");
// #endif

//         u8g2->drawStr(62, 38, "Radio:");    u8g2->drawStr(120, 38, (radio_online) ? "+" : "-");

// #ifdef HAS_PMU
//         u8g2->drawStr(62, 54, "Power:");    u8g2->drawStr(120, 54, (PMU) ? "+" : "-");
// #endif

//         u8g2->sendBuffer();

//     }
// #endif
// }



// static uint8_t ledState = LOW;
// static const uint32_t debounceDelay = 50;
// static uint32_t lastDebounceTime = 0;

// void flashLed() {
// #ifdef BOARD_LED
//     if ((millis() - lastDebounceTime) > debounceDelay) {
//         ledState = !ledState;
//         if (ledState) {
//             digitalWrite(BOARD_LED, LED_ON);
//         }
//         else {
//             digitalWrite(BOARD_LED, !LED_ON);
//         }
//         lastDebounceTime = millis();
//     }
// #endif
// }



