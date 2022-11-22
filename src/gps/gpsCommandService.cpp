#include "gpsCommandService.h"
#include "gpsService.h"

GPSCommandService::GPSCommandService() {
    addCommand(Command("/getGPS", "Get the GPS from the device", GPSMessageType::getGPS, 1,
        [this](String args) {
        return GPSService::getInstance().getGPSUpdatedWait(0);
    }));
}
