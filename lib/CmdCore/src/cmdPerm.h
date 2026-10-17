#pragma once

#include <cstdint>

/** Who may run a command. */
enum class Perm : uint8_t {
    /** Any channel. */
    OPEN,
    /** The serial console, or a remote channel with a valid signature. */
    SIGNED,
    /** Only the serial console (keys and credentials typed on the bench or at provisioning). */
    LOCAL_ONLY,
};

/** Channel a command line arrived on. */
enum class Origin : uint8_t {
    SERIAL_CONSOLE,
    MQTT,
    LORA,
};

/**
 * @return true if a command with @p perm may run when it arrived on @p origin, with
 *         @p signedValid telling whether it carried a valid signature.
 */
bool commandPermitted(Perm perm, Origin origin, bool signedValid);
