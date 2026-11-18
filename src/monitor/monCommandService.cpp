#include "monCommandService.h"
#include "monService.h"

monCommandService::monCommandService() {
    addCommand(Command("/install",
                       "Install mode, awake and reporting often: /install [seconds] (600, 0 "
                       "stops, up to 3600)",
                       Perm::SIGNED,
                       [](String args) { return MonService::getInstance().install(args); }));
}
