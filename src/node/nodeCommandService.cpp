#include "nodeCommandService.h"

#include "bme280Math.h"
#include "cmdText.h"
#include "devices/initDevices.h"
#include "loramesh/loraMeshService.h"
#include "nodeService.h"
#include "sensor/bme280.h"

namespace {

/** A setting that is stored and takes effect after a restart. */
Command storedSetting(const char* name, const char* description, const String& usage,
                      Command::Callback callback) {
    return Command(name, description, Perm::SIGNED, std::move(callback))
        .withUsage(usage)
        .withCode(NODE_ERR_VERIFYING, "The new image is still being verified. Try again later")
        .withCode(NODE_ERR_STORE, "The setting could not be stored");
}

}  // namespace

NodeCommandService::NodeCommandService() {
    addCommand(Command("/role", "Show the node role (gateway or sensor)", Perm::OPEN,
                       [](String) { return NodeService::getInstance().describeRole(); }));
    addCommand(storedSetting("/role.set", "Set the node role (restarts)", "<gateway or sensor>",
                             [](String args) { return NodeService::getInstance().setRole(args); }));
    addCommand(Command("/sleep", "Show the sleep settings and the sleeps since power-on",
                       Perm::OPEN, [](String) {
                           return NodeService::getInstance().describeSleep() + "; " +
                                  LoRaMeshService::getInstance().sleepReport().c_str();
                       }));
    addCommand(
        storedSetting("/sleep.set", "Set the sensor sleep mode (restarts)", "<none, light or deep>",
                      [](String args) { return NodeService::getInstance().setSleepMode(args); }));
    addCommand(storedSetting(
        "/sleep.fraction",
        "Set the minimum sleep share of the superframe on the network manager (restarts)",
        String("<percent from 0 to ") + MAX_SLEEP_FRACTION_PERCENT + ">",
        [](String args) { return NodeService::getInstance().setSleepFraction(args); }));
    addCommand(Command("/power", "Show supply, battery and charger state", Perm::OPEN,
                       [](String) { return String(InitDevices::powerReport().c_str()); }));
    addCommand(
        Command("/sensor", "Show the BME280 or BMP280 sensor and a new measurement", Perm::OPEN,
                [](String) { return CommandReply(Bme280Sensor::getInstance().describe()); })
            .withAllowance(bme280MeasureTimeMs(true))
            .withCode(SENSOR_ERR_NOT_FOUND, "No sensor found (BME280 or BMP280 at 0x76 or 0x77)")
            .withCode(SENSOR_ERR_NO_MEASUREMENT, "The sensor gave no measurement")
            .withCode(SENSOR_ERR_NO_SUPPORT, "This firmware has no sensor support"));
    constexpr uint32_t IDENTIFY_DEFAULT_S = 10;
    constexpr uint32_t IDENTIFY_MAX_S = 600;
    addCommand(
        Command("/identify", "Blink the LEDs to find the board", Perm::OPEN,
                [](String args) -> CommandReply {
                    uint32_t seconds = IDENTIFY_DEFAULT_S;
                    if (args.length() > 0 &&
                        (!parseDecimal(args.c_str(), IDENTIFY_MAX_S, seconds) || seconds == 0)) {
                        return CommandStatus::ARG;
                    }
                    return String(InitDevices::identify(seconds).c_str());
                })
            .withUsage(String("[seconds] (default ") + IDENTIFY_DEFAULT_S + ", up to " +
                       IDENTIFY_MAX_S + ")"));
}
