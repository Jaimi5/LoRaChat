#include "metadataCommandService.h"
#include "metadata.h"

MetadataCommandService::MetadataCommandService() {
    addCommand(Command("/getMetadata", "Get the metadata of the device", appPort::MetadataApp, 1,
        [this](String args) {
        return "Not implemented yet";
    }));
}