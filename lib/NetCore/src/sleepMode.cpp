#include "sleepMode.h"

#include <algorithm>
#include <cctype>

const char* sleepModeName(SleepMode mode) {
    switch (mode) {
        case SleepMode::LIGHT:
            return "light";
        case SleepMode::DEEP:
            return "deep";
        case SleepMode::NONE:
            break;
    }
    return "none";
}

bool parseSleepMode(const std::string& text, SleepMode& out) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (SleepMode mode : {SleepMode::NONE, SleepMode::LIGHT, SleepMode::DEEP}) {
        if (lower == sleepModeName(mode)) {
            out = mode;
            return true;
        }
    }
    return false;
}

bool sleepModeFromValue(uint8_t value, SleepMode& out) {
    if (value > static_cast<uint8_t>(SleepMode::DEEP)) return false;
    out = static_cast<SleepMode>(value);
    return true;
}

bool parseSleepFraction(const std::string& text, uint8_t& percent) {
    if (text.empty() || text.size() > 3) return false;
    unsigned value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value > MAX_SLEEP_FRACTION_PERCENT) return false;
    percent = static_cast<uint8_t>(value);
    return true;
}

SleepDecision decideSleep(SleepMode mode, bool deepRequested, const SleepBlockers& blockers) {
    if (mode == SleepMode::NONE) return SleepDecision::MODE_NONE;
    if (blockers.gateway) return SleepDecision::GATEWAY;
    if (blockers.pendingVerify) return SleepDecision::PENDING_VERIFY;
    if (blockers.maintenanceWindow) return SleepDecision::MAINTENANCE_WINDOW;
    if (blockers.displayAwake) return SleepDecision::DISPLAY_AWAKE;
    if (deepRequested && mode != SleepMode::DEEP) return SleepDecision::NO_DEEP_SLEEP;
    return SleepDecision::ALLOW;
}

const char* sleepDecisionName(SleepDecision decision) {
    switch (decision) {
        case SleepDecision::ALLOW:
            return "allow";
        case SleepDecision::MODE_NONE:
            return "mode none";
        case SleepDecision::NO_DEEP_SLEEP:
            return "no deep sleep";
        case SleepDecision::GATEWAY:
            return "gateway";
        case SleepDecision::PENDING_VERIFY:
            return "pending verify";
        case SleepDecision::MAINTENANCE_WINDOW:
            return "maintenance window";
        case SleepDecision::DISPLAY_AWAKE:
            return "display awake";
    }
    return "?";
}
