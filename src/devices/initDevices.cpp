#include "initDevices.h"
#include <Arduino.h>
#include "config.h"

#include <functional>
#include <memory>

#include "esp_timer.h"
#if defined(T_BEAM)
#include <XPowersLib.h>
#endif

#define PMU_WIRE_PORT Wire

static const char* ID_TAG = "InitDevices";

#ifdef HAS_PMU
static std::unique_ptr<XPowersLibInterface> PMU;

static TaskHandle_t pmuIrqTaskHandle = nullptr;
static std::function<void()> powerKeyHandler;

// The charge LED follows the charger (on while charging, off when full, blinking on a charge
// fault) when a battery is fitted. Without one it stays off: there is nothing to charge.
static void applyChargeLed() {
    PMU->setChargingLedMode(PMU->isBatteryConnect() ? XPOWERS_CHG_LED_CTRL_CHG
                                                    : XPOWERS_CHG_LED_OFF);
}

// A charge fault (e.g. the safety timer) stays latched until VBUS is removed; switching the
// charger off and on clears it as well.
static void restartCharger() {
    constexpr uint32_t CHARGER_OFF_MS = 10;
    if (PMU->getChipModel() == XPOWERS_AXP2101) {
        auto* axp = static_cast<XPowersAXP2101*>(PMU.get());
        axp->disableCellbatteryCharge();
        delay(CHARGER_OFF_MS);
        axp->enableCellbatteryCharge();
    } else if (PMU->getChipModel() == XPOWERS_AXP192) {
        auto* axp = static_cast<XPowersAXP192*>(PMU.get());
        axp->disableCharge();
        delay(CHARGER_OFF_MS);
        axp->enableCharge();
    }
}

/**
 * @brief Probes one PMU model. The destructor of a failed probe ends the shared I2C bus, so the
 *        bus is started again for the next probe and for the display.
 */
template <typename Chip>
static std::unique_ptr<XPowersLibInterface> probePmu(const char* name) {
    auto chip = std::make_unique<Chip>(PMU_WIRE_PORT);
    if (chip->init()) {
        ESP_LOGI(ID_TAG, "Power chip %s found", name);
        return chip;
    }
    chip.reset();
    PMU_WIRE_PORT.begin((int)I2C_SDA, (int)I2C_SCL);
    return nullptr;
}

static void onPmuIrq() {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(pmuIrqTaskHandle, &woken);
    if (woken) portYIELD_FROM_ISR();
}

// The PMU holds its IRQ line low until the status is cleared over I2C, which cannot be done
// from the interrupt handler.
static void pmuIrqTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        PMU->getIrqStatus();
        bool pressed = PMU->isPekeyShortPressIrq();
        bool batteryChanged = PMU->isBatInsertIrq() || PMU->isBatRemoveIrq();
        PMU->clearIrqStatus();
        if (batteryChanged) applyChargeLed();
        if (pressed && powerKeyHandler) powerKeyHandler();
    }
}

void InitDevices::startPmuIrqTask() {
    if (xTaskCreate(pmuIrqTask, "PmuIrq", 3072, nullptr, 2, &pmuIrqTaskHandle) != pdPASS) {
        ESP_LOGE(ID_TAG, "PMU interrupt task creation failed");
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

void InitDevices::init(bool deepSleepWake) {
#if defined(T_BEAM)
    initTBeam(deepSleepWake);
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

bool InitDevices::readPower(PowerState& out) {
#ifdef HAS_PMU
    if (!PMU) return false;
    out.externalPower = PMU->isVbusIn();
    out.batteryMv = PMU->isBatteryConnect() ? PMU->getBattVoltage() : 0;
    return true;
#else
    return false;
#endif
}

std::string InitDevices::powerReport() {
#ifdef HAS_PMU
    if (!PMU) return "pmu=none";
    char text[220];
    int length = snprintf(text, sizeof(text), "pmu=%s vbus=%s battery=%s %umV %d%% charging=%s",
                          PMU->getChipModel() == XPOWERS_AXP2101 ? "AXP2101" : "AXP192",
                          PMU->isVbusIn() ? "yes" : "no", PMU->isBatteryConnect() ? "yes" : "no",
                          PMU->getBattVoltage(), PMU->getBatteryPercent(),
                          PMU->isCharging() ? "yes" : "no");
    if (PMU->getChipModel() == XPOWERS_AXP2101) {
        static const char* STATES[] = {"TRICKLE", "PRE", "CC", "CV", "DONE", "STOP"};
        auto* axp = static_cast<XPowersAXP2101*>(PMU.get());
        size_t state = static_cast<size_t>(axp->getChargerStatus());
        snprintf(text + length, sizeof(text) - length,
                 " state=%s s1=%02x s2=%02x ts=%02x batdet=%02x led=%02x timer=%02x icc=%02x",
                 state < sizeof(STATES) / sizeof(STATES[0]) ? STATES[state] : "?",
                 axp->readRegister(XPOWERS_AXP2101_STATUS1),
                 axp->readRegister(XPOWERS_AXP2101_STATUS2),
                 axp->readRegister(XPOWERS_AXP2101_TS_PIN_CTRL),
                 axp->readRegister(XPOWERS_AXP2101_BAT_DET_CTRL),
                 axp->readRegister(XPOWERS_AXP2101_CHGLED_SET_CTRL),
                 axp->readRegister(XPOWERS_AXP2101_CHG_TIMEOUT_SET_CTRL),
                 axp->readRegister(XPOWERS_AXP2101_ICC_CHG_SET));
    } else if (PMU->getChipModel() == XPOWERS_AXP192) {
        auto* axp = static_cast<XPowersAXP192*>(PMU.get());
        snprintf(text + length, sizeof(text) - length, " status=%02x mode=%02x chgctl=%02x",
                 axp->readRegister(XPOWERS_AXP192_STATUS),
                 axp->readRegister(XPOWERS_AXP192_MODE_CHGSTATUS),
                 axp->readRegister(XPOWERS_AXP192_CHARGE1));
    }
    return text;
#else
    return "pmu=none";
#endif
}

static TaskHandle_t identifyTaskHandle = nullptr;
static volatile int64_t identifyUntilUs = 0;

static void identifyTask(void*) {
    constexpr uint32_t BLINK_MS = 250;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#ifdef HAS_PMU
        if (PMU) PMU->setChargingLedMode(XPOWERS_CHG_LED_BLINK_4HZ);
#endif
        bool on = false;
        while (esp_timer_get_time() < identifyUntilUs) {
            on = !on;
            digitalWrite(LED_PIN, on ? LED_ON : LED_OFF);
            vTaskDelay(pdMS_TO_TICKS(BLINK_MS));
        }
        digitalWrite(LED_PIN, LED_OFF);
#ifdef HAS_PMU
        if (PMU) applyChargeLed();
#endif
    }
}

std::string InitDevices::identify(uint32_t seconds) {
    identifyUntilUs = esp_timer_get_time() + static_cast<int64_t>(seconds) * 1000000;
    if (identifyTaskHandle == nullptr &&
        xTaskCreate(identifyTask, "Identify", 2048, nullptr, 1, &identifyTaskHandle) != pdPASS) {
        identifyTaskHandle = nullptr;
        return "Identify task creation failed";
    }
    xTaskNotifyGive(identifyTaskHandle);
    return "Blinking for " + std::to_string(seconds) + " s";
}

void InitDevices::initTBeam(bool deepSleepWake) {
    // After reset the pin's pull-down lights the LED; drive it off.
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);
    beginPower(deepSleepWake);
}

bool InitDevices::beginPower(bool deepSleepWake) {
#ifdef HAS_PMU
    ESP_LOGV(ID_TAG, "beginPower");

    if (!PMU) PMU = probePmu<XPowersAXP2101>("AXP2101");
    if (!PMU) PMU = probePmu<XPowersAXP192>("AXP192");
    if (!PMU) {
        ESP_LOGE(ID_TAG, "No power chip (AXP192 or AXP2101) answers on I2C: battery, charger "
                         "and radio supply are not managed");
        return false;
    }


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
        PMU->enableIRQ(XPOWERS_AXP192_PKEY_SHORT_IRQ | XPOWERS_AXP192_BAT_INSERT_IRQ |
                       XPOWERS_AXP192_BAT_REMOVE_IRQ);
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
        PMU->enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_BAT_INSERT_IRQ |
                       XPOWERS_AXP2101_BAT_REMOVE_IRQ);
    }
    if (!deepSleepWake) restartCharger();
    applyChargeLed();
    PMU->clearIrqStatus();
    startPmuIrqTask();

    PMU->enableSystemVoltageMeasure();
    PMU->enableVbusVoltageMeasure();
    PMU->enableBattVoltageMeasure();

    static const struct {
        uint8_t channel;
        const char* name;
    } RAILS[] = {{XPOWERS_DCDC1, "DC1"}, {XPOWERS_DCDC2, "DC2"},   {XPOWERS_DCDC3, "DC3"},
                 {XPOWERS_DCDC4, "DC4"}, {XPOWERS_DCDC5, "DC5"},   {XPOWERS_LDO2, "LDO2"},
                 {XPOWERS_LDO3, "LDO3"}, {XPOWERS_ALDO1, "ALDO1"}, {XPOWERS_ALDO2, "ALDO2"},
                 {XPOWERS_ALDO3, "ALDO3"}, {XPOWERS_ALDO4, "ALDO4"}, {XPOWERS_BLDO1, "BLDO1"},
                 {XPOWERS_BLDO2, "BLDO2"}};
    std::string rails;
    for (const auto& rail : RAILS) {
        if (!PMU->isChannelAvailable(rail.channel) || !PMU->isPowerChannelEnable(rail.channel)) {
            continue;
        }
        rails += std::string(" ") + rail.name + "=" +
                 std::to_string(PMU->getPowerChannelVoltage(rail.channel));
    }
    ESP_LOGI(ID_TAG, "PMU rails on (mV):%s", rails.c_str());

    PMU->setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
#endif
    return true;
}