#pragma once

/**
 * @brief Text commands over the USB serial port.
 *
 * Each line received on Serial runs as a command through the MessageManager and the reply is
 * printed. Reading is interrupt driven (Serial.onReceive), so nothing polls the UART.
 */
class SerialConsole {
public:
    static void begin();
};
