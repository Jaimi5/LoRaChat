#pragma once

#include <atomic>
#include <cstdint>
#include <string>

/** Seconds of install mode when /install has no argument. */
constexpr uint32_t INSTALL_DEFAULT_SECONDS = 600;
/** Longest install mode in seconds. */
constexpr uint32_t INSTALL_MAX_SECONDS = 3600;
/** Shortest time between two monitor reports in install mode. */
constexpr uint32_t INSTALL_MIN_REPORT_MS = 10000;
/** Superframes between two monitor reports in install mode. */
constexpr uint32_t INSTALL_REPORT_SUPERFRAMES = 2;

/**
 * @brief Install mode: for a limited time the node stays awake and reports often, so the
 *        person placing it sees the effect of each move.
 *
 * The state lives in RAM, so a reboot ends the mode. The caller provides the clock in
 * milliseconds, which must not wrap. Any task may call any method.
 */
class InstallMode {
public:
    /**
     * @brief Starts the mode, or sets a new end time while it is active.
     * @param seconds duration, limited to INSTALL_MAX_SECONDS. 0 stops the mode.
     * @return the seconds the mode will last.
     */
    uint32_t start(uint32_t seconds, uint64_t nowMs);

    void stop() { untilMs_ = 0; }

    bool active(uint64_t nowMs) const { return nowMs < untilMs_; }

    /** @return the seconds left, rounded up, or 0 when the mode is off. */
    uint32_t remainingS(uint64_t nowMs) const;

    /** @return the milliseconds left, or 0 when the mode is off. */
    uint64_t remainingMs(uint64_t nowMs) const;

    /**
     * @param superframeMs duration of the mesh superframe, 0 when unknown.
     * @return the time between two reports in install mode: INSTALL_REPORT_SUPERFRAMES
     *         superframes and at least INSTALL_MIN_REPORT_MS.
     */
    static uint32_t reportIntervalMs(uint32_t superframeMs);

private:
    /** Clock value at which the mode ends, 0 while it is off. */
    std::atomic<uint64_t> untilMs_{0};
};
