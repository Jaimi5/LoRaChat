#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "espOtaFlash.h"
#include "otaManifest.h"
#include "otaPolicy.h"

/**
 * @brief Steps shared by every way an update arrives: the WiFi pull and the upload through the
 *        node's access point.
 */
class OtaInstall {
public:
    /** @return what the update policy needs to know about this node. */
    static NodeState nodeState();

    /**
     * @brief Verifies a signed manifest and runs the update policy.
     * @param reason set to the check or policy result, for logs and replies.
     * @return true if the image may be installed.
     */
    static bool accept(const uint8_t* data, size_t size, OtaManifest& out, std::string& reason);

    /** Stops the mesh before the first flash write. It cannot be restarted afterwards. */
    static void stopMesh(const OtaManifest& manifest);

    /**
     * @brief Records the attempt for the boot guard, selects the written image and reboots
     *        into it. Falls back to restartAfterFailure() if the image cannot be selected.
     */
    [[noreturn]] static void rebootInto(EspOtaFlash& flash, const OtaManifest& manifest);

    /**
     * @brief Restarts the running image after a failed install. The failed image is skipped
     *        until the next power-on (OtaPolicy SKIP_FAILED_SINCE_POWER_ON).
     */
    [[noreturn]] static void restartAfterFailure(const OtaManifest& manifest);
};
