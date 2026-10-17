#pragma once

#include <cstdint>
#include <string>

/** How a sensor node's MCU sleeps through the SLEEP slots. Stored in NVS, never renumber. */
enum class SleepMode : uint8_t {
    /** The MCU stays awake; only the radio sleeps. */
    NONE = 0,
    /** Light sleep through each run of SLEEP slots. */
    LIGHT = 1,
    /** Deep sleep through long runs (the node reboots and resumes), light sleep otherwise. */
    DEEP = 2,
};

/** Largest stored minimum sleep fraction of the superframe, in percent. */
constexpr uint8_t MAX_SLEEP_FRACTION_PERCENT = 90;

/** @return "none", "light" or "deep". */
const char* sleepModeName(SleepMode mode);

/** Parses "none", "light" or "deep" (any case). @return false for anything else. */
bool parseSleepMode(const std::string& text, SleepMode& out);

/** Decodes a stored mode value. @return false if @p value is not a mode. */
bool sleepModeFromValue(uint8_t value, SleepMode& out);

/**
 * Parses a whole percentage from 0 to MAX_SLEEP_FRACTION_PERCENT (digits only).
 * @return false, leaving @p percent unchanged, for anything else.
 */
bool parseSleepFraction(const std::string& text, uint8_t& percent);

/** What keeps a node awake when the mesh offers it a sleep. */
struct SleepBlockers {
    /** The node is the network manager, which never sleeps. */
    bool gateway = false;
    /** The boot guard is still verifying a new image. */
    bool pendingVerify = false;
    /** A maintenance WiFi window is open. */
    bool maintenanceWindow = false;
    /** The display is on. */
    bool displayAwake = false;
};

/** Answer to the mesh's sleep request. Values are logged, keep them. */
enum class SleepDecision : uint8_t {
    ALLOW = 0,
    MODE_NONE = 1,
    NO_DEEP_SLEEP = 2,
    GATEWAY = 3,
    PENDING_VERIFY = 4,
    MAINTENANCE_WINDOW = 5,
    DISPLAY_AWAKE = 6,
};

/**
 * @brief Decides whether the MCU may sleep when the mesh asks.
 * @param mode the node's sleep mode.
 * @param deepRequested the mesh asks for a deep sleep (otherwise a light sleep).
 * @param blockers what is running on the node.
 */
SleepDecision decideSleep(SleepMode mode, bool deepRequested, const SleepBlockers& blockers);

/** @return a short name of @p decision for the log. */
const char* sleepDecisionName(SleepDecision decision);
