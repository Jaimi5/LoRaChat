#include "maintWindow.h"

constexpr uint32_t MaintWindow::BOOT_WINDOW_MS;

BootWindowDecision MaintWindow::decideBootWindow(bool gateway, bool pendingVerify,
                                                 bool hasCredentials) {
    if (gateway) return BootWindowDecision::SKIP_GATEWAY;
    if (pendingVerify) return BootWindowDecision::SKIP_PENDING_VERIFY;
    if (!hasCredentials) return BootWindowDecision::SKIP_NO_CREDENTIALS;
    return BootWindowDecision::OPEN;
}
