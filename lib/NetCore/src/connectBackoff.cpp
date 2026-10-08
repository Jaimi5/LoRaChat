#include "connectBackoff.h"

ConnectBackoff::ConnectBackoff(uint32_t initialMs, uint32_t maxMs)
    : initialMs_(initialMs), maxMs_(maxMs), waitMs_(initialMs) {}

bool ConnectBackoff::shouldAttempt(uint32_t nowMs) const {
    return !attempted_ || nowMs - lastAttemptMs_ >= waitMs_;
}

void ConnectBackoff::onAttempt(uint32_t nowMs) {
    lastAttemptMs_ = nowMs;
    attempted_ = true;
}

void ConnectBackoff::onSuccess() {
    waitMs_ = initialMs_;
}

void ConnectBackoff::onFailure() {
    waitMs_ = waitMs_ > maxMs_ / 2 ? maxMs_ : waitMs_ * 2;
}
