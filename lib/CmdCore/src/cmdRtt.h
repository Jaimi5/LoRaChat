#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/** Destinations the estimator keeps at least. */
constexpr size_t RTT_DEFAULT_ENTRIES = 16;

/** The path a round trip time was measured on, or is asked for. */
struct RttPath {
    uint16_t dst = 0;
    /** Hops to the destination (h). */
    uint8_t hops = 0;
    uint16_t nextHop = 0;
    /** Duration of the mesh superframe (S). */
    uint32_t superframeMs = 0;
};

/**
 * @brief Measured time from a command to its reply, per destination (RFC 6298).
 *
 * A sample is R = time to the reply - w, where w is the wait of the gateway for its own slot.
 * The first sample gives SRTT = R and RTTVAR = R / 2. Later samples give
 * RTTVAR = 3/4 RTTVAR + 1/4 |SRTT - R| and SRTT = 7/8 SRTT + 1/8 R. A request that was sent
 * twice gives no sample (Karn's rule). An entry belongs to one hop count and next hop: another
 * one starts it again. Another superframe scales SRTT by S new / S old and doubles RTTVAR.
 *
 * The table has a fixed number of entries and replaces the one that was updated longest ago.
 * It is not thread safe: one task owns it.
 */
class RttEstimator {
public:
    explicit RttEstimator(size_t entries = RTT_DEFAULT_ENTRIES);

    /**
     * @brief Adds the round trip of one request.
     * @param path the path when the request was sent.
     * @param elapsedMs time from the transmission request to the reply.
     * @param slotWaitMs the wait for the gateway's slot at the request (w).
     * @param retransmitted the request was sent twice.
     * @return false if the request gave no sample.
     */
    bool addSample(const RttPath& path, uint32_t elapsedMs, uint32_t slotWaitMs,
                   bool retransmitted);

    /**
     * @brief Gives the estimate for @p path as it is now. An entry measured over another hop
     *        count or next hop is dropped, and one measured with another superframe is scaled.
     * @param srttMs the smoothed round trip time, without the wait for the gateway's slot.
     * @return false if the destination has no samples on this path.
     */
    bool estimate(const RttPath& path, uint32_t& srttMs);

    /** @return the variation of the estimate of @p dst, or 0 without samples. */
    uint32_t variationMs(uint16_t dst) const;

    /** @return the number of destinations with samples. */
    size_t size() const;

private:
    struct Entry {
        bool used = false;
        RttPath path;
        uint32_t srttMs = 0;
        uint32_t rttvarMs = 0;
        /** Order of the last update. Larger is newer. */
        uint32_t updated = 0;
    };

    Entry* find(uint16_t dst);
    /** Applies the invalidation rules. @return false if the entry was dropped. */
    static bool follow(Entry& entry, const RttPath& path);

    std::vector<Entry> entries_;
    uint32_t updates_ = 0;
};
