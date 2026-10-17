#pragma once

/**
 * @brief Text commands over the USB serial port.
 *
 * The UART event (Serial.onReceive) only wakes the console task, so nothing polls the UART.
 * That task reads the lines and hands them to the CommandRouter, which runs them in its own
 * task. "@<dst> <line>" sends the line to node dst over LoRa; a signed line ends with
 * " #<counter>.<tag>" (scripts/ota_tools/lmcmd.py). Replies follow a "> <command name>" line;
 * the arguments are not echoed, since they can hold keys. Replies of remote nodes arrive as
 * "< <node> [<request>] <reply>".
 */
class SerialConsole {
public:
    static void begin();
};
