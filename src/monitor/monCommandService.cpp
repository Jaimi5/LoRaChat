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
    addCommand(
        Command("/report.set", "Set the monitor report interval (no restart)", Perm::SIGNED,
                [](String args) { return MonService::getInstance().setReportInterval(args); })
            .withUsage(String("<seconds from ") + REPORT_INTERVAL_MIN_S + " to " +
                       REPORT_INTERVAL_MAX_S + ", or default>")
            .withCode(MON_ERR_STORE, "The interval could not be stored"));
}
