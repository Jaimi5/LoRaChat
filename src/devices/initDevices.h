// Board power (AXP192 or AXP2101, detected at runtime), the power key and the charge LED.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "cmdTyped.h"

/** Battery and supply as read from the PMU. */
struct PowerState {
    /** Battery voltage, 0 without a battery. */
    uint32_t batteryMv = 0;
    /** USB or the solar charger feeds the board. */
    bool externalPower = false;
    /** The charger is charging the battery. */
    bool charging = false;
    /** A battery is fitted. */
    bool batteryPresent = false;
};

class InitDevices {
public:
    /**
     * @brief Powers the board's rails through the PMU.
     * @param deepSleepWake the node woke from deep sleep: the charger keeps running as it was.
     */
    static void init(bool deepSleepWake);

    /**
     * @brief Sets the function called when the PMU power button is pressed briefly.
     *
     * Call it before init(). The handler runs in a task, not in the interrupt.
     */
    static void onPowerKeyShortPress(std::function<void()> handler);

    /**
     * @brief Makes the power key task look at the PMU interrupt line now. The line gives no
     *        interrupt for an event that happened while the MCU slept.
     */
    static void checkPowerKey();

    /**
     * @brief Arms the power key as a wake-up source before a light sleep, or disarms it
     *        before a deep sleep, which the key must not end. Does nothing when
     *        PMU_KEY_WAKES_LIGHT_SLEEP is 0.
     */
    static void armPowerKeyWake(bool lightSleep);

    /** @return true while the PMU holds its interrupt line low: an event waits to be read. */
    static bool powerKeyPending();

    /**
     * @return the mutex that every task holds while it reads a register of a chip on the I2C
     *         bus. The display only writes and does not need it.
     */
    static std::mutex& i2cMutex();

    /** @return true if the PMU answers on I2C, or if the board has no PMU. */
    static bool pmuResponds();

    /**
     * Reads battery and supply from the PMU. Any task may call it.
     * @return false if the board has no PMU.
     */
    static bool readPower(PowerState& out);

    /**
     * @brief Writes the reply fields of /power: the chip (0 none, 1 AXP192, 2 AXP2101) and,
     *        with a chip, external supply, battery fitted, battery mV, battery percent,
     *        charging, the charger state of an AXP2101 and raw registers.
     *
     * The registers of an AXP2101 are STATUS1, STATUS2, TS pin control, battery detection,
     * charge LED, charge timer and charge current, in that order. Those of an AXP192 are
     * status, mode and charge control 1.
     */
    static TypedResult powerInto(Fields& out);

    /**
     * @brief Blinks the user LED and the charge LED for @p seconds so the board can be found.
     *        A second call while blinking sets a new end time.
     * @return false if the blink task could not be started.
     */
    static bool identify(uint32_t seconds);

private:
    static void initTBeam(bool deepSleepWake);
    static bool beginPower(bool deepSleepWake);
    static void startPmuIrqTask();
};