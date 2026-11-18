#include "monCommandService.h"
#include "installMode.h"
#include "monService.h"

monCommandService::monCommandService() {
    addCommand(Command("/install", "Install mode, awake and reporting often", Perm::SIGNED,
                       [](String args) { return MonService::getInstance().install(args); })
                   .withUsage(String("[seconds] (default ") + INSTALL_DEFAULT_SECONDS +
                              ", 0 stops, up to " + INSTALL_MAX_SECONDS + ")"));
}
