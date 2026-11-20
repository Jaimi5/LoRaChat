#include "nodeCommandService.h"

#include "bme280Math.h"
#include "devices/initDevices.h"
#include "loramesh/loraMeshService.h"
#include "nodeService.h"
#include "sensor/bme280.h"

namespace {

/** A setting that is stored and takes effect after a restart. */
Command storedSetting(uint8_t id, const char* name, const char* description) {
    return Command(id, name, description, Perm::SIGNED)
        .withCode(NODE_ERR_VERIFYING, "The new image is still being verified. Try again later")
        .withCode(NODE_ERR_STORE, "The setting could not be stored", "x")
        .withCode(NODE_ERR_RESTART, "The setting is stored. The restart could not be scheduled");
}

}  // namespace

NodeCommandService::NodeCommandService() {
    addCommand(Command(0x10, "/role", "Show the node role (gateway or sensor)", Perm::OPEN)
                   .reply("role:e(sensor,gateway) src:e(default,stored)", "{0} ({1})")
                   .run([](const Fields&, Fields& out) {
                       return NodeService::getInstance().roleInto(out);
                   }));
    addCommand(storedSetting(0x11, "/role.set", "Set the node role (restarts)")
                   .args("role:e(sensor,gateway)")
                   .reply("", "Role set to {a0}, restarting")
                   .run([](const Fields& in, Fields&) {
                       return NodeService::getInstance().setRole(in.u(0));
                   })
                   .withUsage("<gateway or sensor>"));
    addCommand(
        Command(0x12, "/sleep", "Show the sleep settings and the sleeps since power-on", Perm::OPEN)
            .reply("mode:e(none,light,deep) src:e(default,stored) frac:u? fdef:e(default)? "
                   "light:u lights:u deep:u deeps:u keys:u?",
                   "sleep {0} ({1}), fraction [{2} % (stored)][{3}], light {4} x {5} s, deep {6} "
                   "x {7} s since power-on[, {8} light sleeps ended by the power key]")
            .run([](const Fields&, Fields& out) {
                constexpr size_t FIRST_COUNT_FIELD = 4;
                NodeService::getInstance().sleepInto(out);
                LoRaMeshService::getInstance().sleepCountsInto(out, FIRST_COUNT_FIELD);
                return TypedResult();
            }));
    addCommand(storedSetting(0x13, "/sleep.set", "Set the sensor sleep mode (restarts)")
                   .args("mode:e(none,light,deep)")
                   .reply("", "Sleep mode set to {a0}, restarting")
                   .run([](const Fields& in, Fields&) {
                       return NodeService::getInstance().setSleepMode(in.u(0));
                   }));
    addCommand(
        storedSetting(
            0x14, "/sleep.fraction",
            "Set the minimum sleep share of the superframe on the network manager (restarts)")
            .args("percent:u")
            .reply("", "Sleep fraction set to {a0} %, restarting")
            .run([](const Fields& in, Fields&) {
                return NodeService::getInstance().setSleepFraction(in.u(0));
            })
            .withUsage(String("<percent from 0 to ") + MAX_SLEEP_FRACTION_PERCENT + ">"));
    addCommand(Command(0x16, "/power", "Show supply, battery and charger state", Perm::OPEN)
                   .reply("pmu:e(none,AXP192,AXP2101) vbus:e(no,yes)? battery:e(no,yes)? mv:u? "
                          "pct:i? charging:e(no,yes)? state:e(TRICKLE,PRE,CC,CV,DONE,STOP)? reg:r?",
                          "pmu={0}[ vbus={1} battery={2} {3}mV {4}% charging={5}][ state={6}][ "
                          "reg={7}]")
                   .run([](const Fields&, Fields& out) { return InitDevices::powerInto(out); }));
    addCommand(
        Command(0x18, "/sensor", "Show the BME280 or BMP280 sensor and a new measurement",
                Perm::OPEN)
            .reply("chip:e(BMP280,BME280) addr:h1 t:i/100 p:u/100 rh:u?/100 age:u?",
                   "{0} at 0x{1}: {2} C[ {4} %RH] {3} hPa[, read {5} s ago, the sensor does not "
                   "answer now]")
            .run([](const Fields&, Fields& out) {
                return Bme280Sensor::getInstance().measureInto(out);
            })
            .withAllowance(bme280MeasureTimeMs(true))
            .withCode(SENSOR_ERR_NOT_FOUND, "No sensor found (BME280 or BMP280 at 0x76 or 0x77)")
            .withCode(SENSOR_ERR_NO_MEASUREMENT, "The sensor gave no measurement")
            .withCode(SENSOR_ERR_NO_SUPPORT, "This firmware has no sensor support"));
    constexpr uint32_t IDENTIFY_DEFAULT_S = 10;
    constexpr uint32_t IDENTIFY_MAX_S = 600;
    addCommand(Command(0x1A, "/identify", "Blink the LEDs to find the board", Perm::OPEN)
                   .args("seconds:u?")
                   .reply("seconds:u", "Blinking for {0} s")
                   .run([](const Fields& in, Fields& out) -> TypedResult {
                       const uint32_t seconds = in.has(0) ? in.u(0) : IDENTIFY_DEFAULT_S;
                       if (seconds == 0 || seconds > IDENTIFY_MAX_S) return CommandStatus::ARG;
                       if (!InitDevices::identify(seconds)) return TypedResult(NODE_ERR_IDENTIFY);
                       out.setU(0, seconds);
                       return TypedResult();
                   })
                   .withUsage(String("[seconds] (default ") + IDENTIFY_DEFAULT_S + ", up to " +
                              IDENTIFY_MAX_S + ")")
                   .withCode(NODE_ERR_IDENTIFY, "The blink task could not be started"));
}
