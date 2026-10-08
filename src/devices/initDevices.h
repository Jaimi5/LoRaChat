// This file initializes the device that is enabled by the config.h file or using the platformio.ini
// file.

class InitDevices {
public:
    static void init();

    /** @return true if the PMU answers on I2C, or if the board has no PMU. */
    static bool pmuResponds();

private:
    static void initTBeam();
    static bool beginPower();
    static void initMakerfabsSenseLoraMoisture();
};