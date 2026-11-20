#pragma once

#include <cstddef>
#include <cstdint>

/** A restart never comes earlier than this after its request. */
constexpr uint32_t RESTART_MIN_DELAY_MS = 1000;
/** Time given to the last frame to be on the air after the TX queue has emptied. */
constexpr uint32_t RESTART_AIR_TIME_MS = 2000;
/** Margin added to two superframes for the longest wait. */
constexpr uint32_t RESTART_CAP_MARGIN_MS = 5000;
/** Longest wait while the superframe is unknown. */
constexpr uint32_t RESTART_CAP_UNKNOWN_MS = 180000;

/**
 * @param superframeMs duration of the mesh superframe, 0 when unknown.
 * @return the longest time a restart waits after its request: two superframes and
 *         RESTART_CAP_MARGIN_MS, or RESTART_CAP_UNKNOWN_MS without a superframe.
 */
uint32_t restartCapMs(uint32_t superframeMs);

/**
 * @brief Decides when a node that was asked to restart by a command may do so, so that the reply
 *        to the command leaves over LoRa first.
 *
 * The reply joins the mesh TX queue after the command has run and leaves in a TX data slot of
 * the node. The restart waits RESTART_MIN_DELAY_MS, then until the queue is seen empty and
 * RESTART_AIR_TIME_MS more. The messages leave oldest first, so one that joins the queue after
 * it was seen empty does not hold the restart. After restartCapMs() the restart waits no longer.
 *
 * One gate serves one restart request. The caller asks it periodically.
 */
class RestartGate {
public:
    /**
     * @param elapsedMs time since the restart was requested.
     * @param txQueued messages in the mesh TX data queue now.
     * @param superframeMs duration of the mesh superframe now, 0 when unknown.
     * @return true once the node may restart.
     */
    bool due(uint32_t elapsedMs, size_t txQueued, uint32_t superframeMs);

private:
    bool queueEmptied_ = false;
    /** Time since the request at which the queue was first seen empty. */
    uint32_t emptiedAtMs_ = 0;
};
