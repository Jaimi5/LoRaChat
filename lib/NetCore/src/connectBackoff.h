#pragma once

#include <cstdint>

/**
 * @brief Exponential backoff between connection attempts.
 *
 * The first attempt is allowed at once. Every failure doubles the wait up to a maximum; a
 * success resets it. Times are milliseconds from a free-running counter (millis()), so
 * comparisons stay correct across its wraparound.
 */
class ConnectBackoff {
public:
    ConnectBackoff(uint32_t initialMs, uint32_t maxMs);

    /** @return true if an attempt may start at @p nowMs. */
    bool shouldAttempt(uint32_t nowMs) const;

    /** Records that an attempt started at @p nowMs. */
    void onAttempt(uint32_t nowMs);

    /** Resets the wait after a successful connection. */
    void onSuccess();

    /** Doubles the wait, up to the maximum, after a failed attempt. */
    void onFailure();

    /** @return the current wait between attempts. */
    uint32_t waitMs() const { return waitMs_; }

private:
    uint32_t initialMs_;
    uint32_t maxMs_;
    uint32_t waitMs_;
    uint32_t lastAttemptMs_ = 0;
    bool attempted_ = false;
};
