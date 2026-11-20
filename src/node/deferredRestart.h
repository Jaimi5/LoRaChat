#pragma once

#include <atomic>
#include <cstdint>

#include "restartGate.h"

/**
 * @brief Restarts the node for a command once the reply to that command has left over LoRa.
 *
 * The reply to a command that came over LoRa waits in the mesh TX queue for the next TX data slot
 * of the node, which can be a whole superframe away. A periodic timer asks RestartGate with the
 * TX queue and the superframe of the mesh: the node restarts at least 1 s after the request, 2 s
 * after the queue was seen empty, and at the latest after two superframes and 5 s (180 s while
 * the superframe is unknown). With an empty queue, as for a command from the serial console or
 * from MQTT, that is 3 s after the request. The node clock is saved right before the restart.
 *
 * The node must stay awake while a restart waits (isPending()).
 */
class DeferredRestart {
public:
    static DeferredRestart& getInstance() {
        static DeferredRestart instance;
        return instance;
    }

    /**
     * @brief Asks for the restart. Safe from any task. A request while one waits changes nothing.
     * @return false if the timer could not be started. No restart follows then.
     */
    bool request();

    /** @return true while a requested restart waits. Any task may call it. */
    bool isPending() const { return pending_; }

private:
    DeferredRestart() = default;

    static void poll(void* parameter);

    std::atomic<bool> pending_{false};
    /** Clock of the request. Written before the timer starts, read by the timer. */
    int64_t requestedUs_ = 0;
    /** Only the timer uses it. */
    RestartGate gate_;
};
