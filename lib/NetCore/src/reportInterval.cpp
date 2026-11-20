#include "reportInterval.h"

#include <algorithm>

bool reportIntervalValid(uint32_t seconds) {
    return seconds >= REPORT_INTERVAL_MIN_S && seconds <= REPORT_INTERVAL_MAX_S;
}

ReportPacing chooseReportPacing(uint32_t defaultMs, uint32_t storedS, bool installActive,
                                uint32_t installIntervalMs) {
    ReportPacing pacing;
    pacing.stored = reportIntervalValid(storedS);
    pacing.configuredMs = pacing.stored ? storedS * 1000 : defaultMs;
    pacing.installShortens = installActive && installIntervalMs < pacing.configuredMs;
    pacing.intervalMs = pacing.installShortens ? installIntervalMs : pacing.configuredMs;
    return pacing;
}

std::string describeReportPacing(const ReportPacing& pacing, uint32_t nextInS) {
    const char* source = pacing.stored ? "stored" : "default";
    std::string text = "report every " + std::to_string(pacing.intervalMs / 1000) + " s (";
    if (pacing.installShortens) {
        text += "install mode, then " + std::to_string(pacing.configuredMs / 1000) + " s " + source;
    } else {
        text += source;
    }
    return text + "), next in " + std::to_string(nextInS) + " s";
}
