// This file initializes the device that is enabled by the config.h file or using the platformio.ini file.

class InitDevices {
public:
    static void init();

private:
    static void initTBeam();
    static bool beginPower();
};