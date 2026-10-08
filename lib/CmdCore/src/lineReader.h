#pragma once

#include <cstddef>
#include <string>

/**
 * @brief Assembles command lines from a byte stream (serial console).
 *
 * A line ends with CR, LF or CRLF. Leading and trailing spaces are removed and empty lines are
 * ignored. A line longer than the maximum is dropped as a whole.
 */
class LineReader {
public:
    static constexpr size_t MAX_LINE = 255;

    /**
     * @brief Adds one byte.
     * @return true when @p byte completes a line; the line is then in @p line.
     */
    bool push(char byte, std::string& line);

private:
    std::string buffer_;
    bool overflow_ = false;
};
