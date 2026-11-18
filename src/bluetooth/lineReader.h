#pragma once

#include <stddef.h>
#include <stdint.h>

// Longest line accepted from the Bluetooth serial console, without the terminator.
#define BT_LINE_MAX 200

/**
 * @brief Assembles lines from a byte stream, one byte at a time.
 *
 * A line ends with CR, LF or CRLF. Control bytes are dropped (a tab becomes a space), trailing
 * spaces are trimmed and empty lines are ignored. A line longer than BT_LINE_MAX is discarded up
 * to its terminator and reported once as TooLong. Bytes >= 0x80 pass through so UTF-8 survives.
 *
 * Has no dependencies so it can be compiled and tested on a host.
 */
class LineReader {
public:
    enum Result : uint8_t { None, Line, TooLong };

    /**
     * @brief Feed one byte.
     *
     * @return Line when a complete line is available through line() (valid until the next push),
     * TooLong when an over-long line has just ended, None otherwise.
     */
    Result push(uint8_t c) {
        if (c == '\n' || c == '\r') {
            bool wasOverflow = overflow;
            size_t n = len;
            len = 0;
            overflow = false;

            if (wasOverflow)
                return TooLong;

            while (n > 0 && buf[n - 1] == ' ')
                n--;
            buf[n] = '\0';
            return n > 0 ? Line : None;
        }

        if (c == '\t')
            c = ' ';
        if (c < 0x20 || c == 0x7F)
            return None;

        if (overflow)
            return None;

        if (len >= BT_LINE_MAX) {
            overflow = true;
            return None;
        }

        buf[len++] = (char)c;
        return None;
    }

    // The completed line, NUL terminated. Only valid right after push() returned Line.
    const char* line() const { return buf; }

    // True when part of a line has been received and is waiting for its terminator.
    bool pending() const { return len > 0 || overflow; }

    // Drop any partial line.
    void reset() {
        len = 0;
        overflow = false;
    }

private:
    char buf[BT_LINE_MAX + 1] = {0};
    size_t len = 0;
    bool overflow = false;
};
