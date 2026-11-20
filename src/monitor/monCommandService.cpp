#include "monCommandService.h"
#include "installMode.h"
#include "monService.h"

monCommandService::monCommandService() {
    addCommand(Command("/install", "Install mode, awake and reporting often", Perm::SIGNED,
                       [](String args) { return MonService::getInstance().install(args); })
                   .withUsage(String("[seconds] (default ") + INSTALL_DEFAULT_SECONDS +
                              ", 0 stops, up to " + INSTALL_MAX_SECONDS + ")"));
    addCommand(Command("/report", "Show the monitor report interval and the next report",
                       Perm::OPEN,
                       [](String) { return MonService::getInstance().describeReport(); }));
    // The text handler stays: a text line may say "default", and its reply names the default.
    addCommand(
        Command(0x7A, "/report.set", "Set the monitor report interval (no restart)", Perm::SIGNED)
            .args("seconds:u")
            .reply("def:u?", "Report interval set to {a0} s[ (the default, {0} s)]")
            .run([](const Fields& in, Fields& out) {
                return MonService::getInstance().setReportInterval(in.u(0), out);
            })
            .runText([](String args) { return MonService::getInstance().setReportInterval(args); })
            .withUsage(String("<seconds from ") + REPORT_INTERVAL_MIN_S + " to " +
                       REPORT_INTERVAL_MAX_S + ", or default>")
            .withCode(MON_ERR_STORE, "The interval could not be stored"));
}
