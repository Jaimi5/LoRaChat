// This file initializes the device that is enabled by the config.h file or using the platformio.ini
// file.

class InitDevices {
public:
    static void init();

    // Deep-sleep wake-up: reconnect to the PMU, which kept its settings through
    // the sleep, and make sure the radio rail is on. Other boards: init().
    static void initPower();

private:
    static void initTBeam();
    static bool beginPower();
    static bool findPmu();
    static void initMakerfabsSenseLoraMoisture();
};