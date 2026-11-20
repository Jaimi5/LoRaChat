#include "monCommandService.h"
#include "commands/commandRouter.h"
#include "installMode.h"
#include "monService.h"

monCommandService::monCommandService() {
    addCommand(Command(0x78, "/install", "Install mode, awake and reporting often", Perm::SIGNED)
                   .args("seconds:u?")
                   .reply("on:u? off:e(off)?", "OK install [{0} s][{1}]")
                   .run([](const Fields& in, Fields& out) {
                       const uint32_t seconds = in.has(0) ? in.u(0) : INSTALL_DEFAULT_SECONDS;
                       return MonService::getInstance().install(seconds, out);
                   })
                   .withUsage(String("[seconds] (default ") + INSTALL_DEFAULT_SECONDS +
                              ", 0 stops, up to " + INSTALL_MAX_SECONDS + ")"));
    addCommand(
        Command(0x79, "/report", "Show the monitor report interval and the next report", Perm::OPEN)
            .reply("every:u src:e(default,stored) next:u then:u?",
                   "report every {0} s ([install mode, then {3} s ]{1}), next in {2} s")
            .run([](const Fields&, Fields& out) {
                return MonService::getInstance().reportInto(out);
            }));
    // The text handler stays for the serial console, where the word "default" stands for 0.
    addCommand(
        Command(0x7A, "/report.set", "Set the monitor report interval (no restart)", Perm::SIGNED)
            .args("seconds:u")
            .reply("stored:u? def:u?", "Report interval set to [{0} s][default ({1} s)]")
            .run([](const Fields& in, Fields& out) {
                return MonService::getInstance().setReportInterval(in.u(0), out);
            })
            .runText([](String args, const Command& self) {
                const bool console =
                    CommandRouter::getInstance().runningOrigin() == Origin::SERIAL_CONSOLE;
                return self.runTyped(console && args.equalsIgnoreCase("default") ? "0" : args);
            })
            .withUsage(String("<seconds from ") + REPORT_INTERVAL_MIN_S + " to " +
                       REPORT_INTERVAL_MAX_S + ", or 0 for the default>")
            .withCode(MON_ERR_STORE, "The interval could not be stored", "x"));
}
