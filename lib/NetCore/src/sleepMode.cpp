#include "sleepMode.h"

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

bool sleepModeFromValue(uint8_t value, SleepMode& out) {
    if (value > static_cast<uint8_t>(SleepMode::DEEP)) return false;
    out = static_cast<SleepMode>(value);
    return true;
}

SleepDecision decideSleep(SleepMode mode, bool deepRequested, const SleepBlockers& blockers) {
    if (mode == SleepMode::NONE) return SleepDecision::MODE_NONE;
    if (blockers.gateway) return SleepDecision::GATEWAY;
    if (blockers.pendingVerify) return SleepDecision::PENDING_VERIFY;
    if (blockers.maintenanceWindow) return SleepDecision::MAINTENANCE_WINDOW;
    if (blockers.displayAwake) return SleepDecision::DISPLAY_AWAKE;
    if (blockers.pmuIrqLow) return SleepDecision::PMU_IRQ_PENDING;
    if (blockers.installMode) return SleepDecision::INSTALL_MODE;
    if (blockers.restartPending) return SleepDecision::RESTART_PENDING;
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
        case SleepDecision::PMU_IRQ_PENDING:
            return "pmu interrupt pending";
        case SleepDecision::INSTALL_MODE:
            return "install mode";
        case SleepDecision::RESTART_PENDING:
            return "restart pending";
    }
    return "?";
}
