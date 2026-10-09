#pragma once

#include <string>

#include "otaManifest.h"
#include "otaPolicy.h"

/**
 * @brief Pulls a signed image from the OTA server and installs it.
 *
 * Runs once WiFi is joined: downloads manifest.bin from the server, verifies its signature and
 * the update policy, stops the mesh, streams firmware.bin into the other app slot, records the
 * attempt for the boot guard and reboots into the new image. The boot guard then decides
 * whether the image is kept.
 */
class OtaWifiPull {
public:
    /**
     * @brief Checks the server and installs a newer image.
     *
     * Returns when there is nothing to install. Once the mesh is stopped it does not return:
     * the node reboots into the new image, or into the running one after a failure, because the
     * mesh cannot be restarted. A failed image is then skipped until the next power-on.
     */
    static void run();

private:
    static std::string serverUrl();
    static bool fetchManifest(const std::string& url, OtaManifest& out);
    static NodeState nodeState();
    [[noreturn]] static void install(const OtaManifest& manifest, const std::string& imageUrl);
};
