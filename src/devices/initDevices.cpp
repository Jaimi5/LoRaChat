#include "initDevices.h"
#include <Arduino.h>
#include "config.h"

#include <functional>
#if defined(T_BEAM)
#include <XPowersLib.h>
#endif

#define PMU_WIRE_PORT Wire

#ifdef HAS_PMU
XPowersLibInterface* PMU = NULL;

static TaskHandle_t powerKeyTaskHandle = nullptr;
static std::function<void()> powerKeyHandler;

static void onPmuIrq() {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(powerKeyTaskHandle, &woken);
    if (woken) portYIELD_FROM_ISR();
}

// The PMU holds its IRQ line low until the status is cleared over I2C, which cannot be done
// from the interrupt handler.
static void powerKeyTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        PMU->getIrqStatus();
        bool pressed = PMU->isPekeyShortPressIrq();
        PMU->clearIrqStatus();
        if (pressed && powerKeyHandler) powerKeyHandler();
    }
}

void InitDevices::startPowerKeyTask() {
    if (xTaskCreate(powerKeyTask, "PowerKey", 3072, nullptr, 2, &powerKeyTaskHandle) != pdPASS) {
        ESP_LOGE("InitDevices", "Power key task creation failed");
        return;
    }
    pinMode(PMU_IRQ, INPUT);
    attachInterrupt(PMU_IRQ, onPmuIrq, FALLING);
}
#endif

void InitDevices::onPowerKeyShortPress(std::function<void()> handler) {
#ifdef HAS_PMU
    powerKeyHandler = std::move(handler);
#endif
}

void InitDevices::init() {
#if defined(T_BEAM)
    initTBeam();
#endif
}

bool InitDevices::pmuResponds() {
#ifdef HAS_PMU
    if (!PMU) return false;
    PMU_WIRE_PORT.beginTransmission(AXP2101_SLAVE_ADDRESS);
    return PMU_WIRE_PORT.endTransmission() == 0;
#else
    return true;
#endif
}

void InitDevices::initTBeam() {
    beginPower();
}

bool InitDevices::beginPower() {
#ifdef HAS_PMU
    ESP_LOGV("InitDevices", "beginPower");

    if (!PMU) {
        PMU = new XPowersAXP2101(PMU_WIRE_PORT);
        if (!PMU->init()) {
            Serial.println("Warning: Failed to find AXP2101 power management");
            delete PMU;
            PMU = NULL;
        } else {
            Serial.println("AXP2101 PMU init succeeded, using AXP2101 PMU");
        }
    }

    if (!PMU) {
        PMU = new XPowersAXP192(PMU_WIRE_PORT);
        if (!PMU->init()) {
            Serial.println("Warning: Failed to find AXP192 power management");
            delete PMU;
            PMU = NULL;
        } else {
            Serial.println("AXP192 PMU init succeeded, using AXP192 PMU");
        }
    }

    if (!PMU) {
        return false;
    }

    // Disable charging LED
    /*
      The default setting is CHGLED is automatically controlled by the PMU.
    - XPOWERS_CHG_LED_OFF,
    - XPOWERS_CHG_LED_BLINK_1HZ,
    - XPOWERS_CHG_LED_BLINK_4HZ,
    - XPOWERS_CHG_LED_ON,
    - XPOWERS_CHG_LED_CTRL_CHG,
    * */
    PMU->setChargingLedMode(XPOWERS_CHG_LED_OFF);


    if (PMU->getChipModel() == XPOWERS_AXP192) {
        // ESP32
        PMU->setProtectedChannel(XPOWERS_DCDC3);
        // OLED and the 3.3 V header pins
        PMU->setPowerChannelVoltage(XPOWERS_DCDC1, 3300);
        PMU->setProtectedChannel(XPOWERS_DCDC1);
        PMU->enablePowerOutput(XPOWERS_DCDC1);
        // LoRa radio
        PMU->setPowerChannelVoltage(XPOWERS_LDO2, 3300);
        PMU->enablePowerOutput(XPOWERS_LDO2);
        // GPS (not used) and the unused DCDC2
        PMU->disablePowerOutput(XPOWERS_LDO3);
        PMU->disablePowerOutput(XPOWERS_DCDC2);

        PMU->setChargeTargetVoltage(XPOWERS_AXP192_CHG_VOL_4V2);

        PMU->disableIRQ(XPOWERS_AXP192_ALL_IRQ);
        PMU->enableIRQ(XPOWERS_AXP192_PKEY_SHORT_IRQ);
    } else if (PMU->getChipModel() == XPOWERS_AXP2101) {
        // Unused channels
        PMU->disablePowerOutput(XPOWERS_DCDC2);
        PMU->disablePowerOutput(XPOWERS_DCDC3);
        PMU->disablePowerOutput(XPOWERS_DCDC4);
        PMU->disablePowerOutput(XPOWERS_DCDC5);
        PMU->disablePowerOutput(XPOWERS_ALDO1);
        PMU->disablePowerOutput(XPOWERS_ALDO4);
        PMU->disablePowerOutput(XPOWERS_BLDO1);
        PMU->disablePowerOutput(XPOWERS_BLDO2);
        PMU->disablePowerOutput(XPOWERS_DLDO1);
        PMU->disablePowerOutput(XPOWERS_DLDO2);
        // GPS (not used) and its backup supply
        PMU->disablePowerOutput(XPOWERS_ALDO3);
        PMU->disablePowerOutput(XPOWERS_VBACKUP);

        // ESP32, OLED and the 3.3 V header pins
        PMU->setProtectedChannel(XPOWERS_DCDC1);
        // LoRa radio
        PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
        PMU->enablePowerOutput(XPOWERS_ALDO2);

        // The T-Beam has no thermistor on the TS pin; with the measurement on, the PMU can
        // treat the battery as out of temperature range and stop charging.
        PMU->disableTSPinMeasure();
        PMU->setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);

        PMU->disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
        PMU->enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
    }
    PMU->clearIrqStatus();
    startPowerKeyTask();

    PMU->enableSystemVoltageMeasure();
    PMU->enableVbusVoltageMeasure();
    PMU->enableBattVoltageMeasure();

    Serial.printf("=========================================\n");
    if (PMU->isChannelAvailable(XPOWERS_DCDC1)) {
        Serial.printf("DC1  : %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_DCDC1) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_DCDC1));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC2)) {
        Serial.printf("DC2  : %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_DCDC2) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_DCDC2));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC3)) {
        Serial.printf("DC3  : %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_DCDC3) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_DCDC3));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC4)) {
        Serial.printf("DC4  : %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_DCDC4) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_DCDC4));
    }
    if (PMU->isChannelAvailable(XPOWERS_DCDC5)) {
        Serial.printf("DC5  : %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_DCDC5) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_DCDC5));
    }
    if (PMU->isChannelAvailable(XPOWERS_LDO2)) {
        Serial.printf("LDO2 : %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_LDO2) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_LDO2));
    }
    if (PMU->isChannelAvailable(XPOWERS_LDO3)) {
        Serial.printf("LDO3 : %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_LDO3) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_LDO3));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO1)) {
        Serial.printf("ALDO1: %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_ALDO1) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_ALDO1));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO2)) {
        Serial.printf("ALDO2: %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_ALDO2) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_ALDO2));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO3)) {
        Serial.printf("ALDO3: %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_ALDO3) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_ALDO3));
    }
    if (PMU->isChannelAvailable(XPOWERS_ALDO4)) {
        Serial.printf("ALDO4: %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_ALDO4) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_ALDO4));
    }
    if (PMU->isChannelAvailable(XPOWERS_BLDO1)) {
        Serial.printf("BLDO1: %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_BLDO1) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_BLDO1));
    }
    if (PMU->isChannelAvailable(XPOWERS_BLDO2)) {
        Serial.printf("BLDO2: %s   Voltage: %04u mV \n",
                      PMU->isPowerChannelEnable(XPOWERS_BLDO2) ? "+" : "-",
                      PMU->getPowerChannelVoltage(XPOWERS_BLDO2));
    }
    Serial.printf("=========================================\n");


    // Set the time of pressing the button to turn off
    PMU->setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    uint8_t opt = PMU->getPowerKeyPressOffTime();
    Serial.print("PowerKeyPressOffTime:");
    switch (opt) {
        case XPOWERS_POWEROFF_4S:
            Serial.println("4 Second");
            break;
        case XPOWERS_POWEROFF_6S:
            Serial.println("6 Second");
            break;
        case XPOWERS_POWEROFF_8S:
            Serial.println("8 Second");
            break;
        case XPOWERS_POWEROFF_10S:
            Serial.println("10 Second");
            break;
        default:
            break;
    }
#endif
    return true;
}