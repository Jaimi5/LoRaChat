#pragma once

#include <cstdint>

/** How good a LoRa link is for placing a node. */
enum class Verdict : uint8_t { NONE, POOR, FAIR, GOOD };

// SNR thresholds in dB. The web app mirrors them in web/src/lib/linkQuality.ts.
// SF9 demodulates down to about -12.5 dB, so a FAIR link keeps about 5 dB of margin.
constexpr float LINK_GOOD_SNR_DB = -2.0F;
constexpr float LINK_FAIR_SNR_DB = -7.0F;

/**
 * @param known false when no signal reading exists for the link.
 * @param snrDb signal to noise ratio of the last packet heard on the link.
 * @return GOOD from LINK_GOOD_SNR_DB up, FAIR from LINK_FAIR_SNR_DB up, POOR below, NONE when
 *         the reading is unknown.
 */
inline Verdict verdictFromSnr(bool known, float snrDb) {
    if (!known) return Verdict::NONE;
    if (snrDb >= LINK_GOOD_SNR_DB) return Verdict::GOOD;
    if (snrDb >= LINK_FAIR_SNR_DB) return Verdict::FAIR;
    return Verdict::POOR;
}

/** @return "GOOD", "FAIR", "POOR" or "NONE". */
inline const char* verdictName(Verdict verdict) {
    switch (verdict) {
        case Verdict::GOOD:
            return "GOOD";
        case Verdict::FAIR:
            return "FAIR";
        case Verdict::POOR:
            return "POOR";
        case Verdict::NONE:
            break;
    }
    return "NONE";
}
