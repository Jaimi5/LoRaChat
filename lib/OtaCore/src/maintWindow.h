#pragma once

#include <cstdint>

/** Whether the maintenance WiFi window opens after boot. Values are logged, keep them. */
enum class BootWindowDecision : uint8_t {
    OPEN = 0,
    SKIP_GATEWAY = 1,
    SKIP_PENDING_VERIFY = 2,
    SKIP_NO_CREDENTIALS = 3,
};

/** Rules of the maintenance WiFi window. */
class MaintWindow {
public:
    static constexpr uint32_t BOOT_WINDOW_MS = 60000;

    /**
     * @brief Decides whether a node opens the WiFi window after boot.
     * @param gateway the node is a gateway, which keeps its own WiFi.
     * @param pendingVerify the running image is still being verified by the boot guard.
     * @param hasCredentials a maintenance hotspot is configured.
     */
    static BootWindowDecision decideBootWindow(bool gateway, bool pendingVerify,
                                               bool hasCredentials);
};
