#pragma once

/**
 * @brief Text commands over the USB serial port.
 *
 * Lines received on Serial are queued from the UART event task (Serial.onReceive), so nothing
 * polls the UART, and run one at a time in a console task with its own stack: commands may
 * hash, verify signatures or write NVS. The reply is printed after the command name; the
 * arguments are not echoed, since they can hold keys.
 */
class SerialConsole {
public:
    static void begin();
};
