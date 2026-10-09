// This file initializes the device that is enabled by the config.h file or using the platformio.ini
// file.
#pragma once

#include <cstdint>
#include <functional>

/** Battery and supply as read from the PMU. */
struct PowerState {
    /** Battery voltage, 0 without a battery. */
    uint32_t batteryMv = 0;
    /** USB or the solar charger feeds the board. */
    bool externalPower = false;
};

class InitDevices {
public:
    static void init();

    /**
     * @brief Sets the function called when the PMU power button is pressed briefly.
     *
     * Call it before init(). The handler runs in a task, not in the interrupt.
     */
    static void onPowerKeyShortPress(std::function<void()> handler);

    /** @return true if the PMU answers on I2C, or if the board has no PMU. */
    static bool pmuResponds();

    /** Reads battery and supply from the PMU. @return false if the board has no PMU. */
    static bool readPower(PowerState& out);

private:
    static void initTBeam();
    static bool beginPower();
    static void startPowerKeyTask();
};