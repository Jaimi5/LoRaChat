#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Text lines of the display: fixed lines at the top and a log of messages below them.
 *
 * There are always @p totalLines lines. push() writes the newest message to the first log line
 * and moves the older ones down, dropping the oldest.
 */
class DisplayLines {
public:
    DisplayLines(size_t staticLines, size_t totalLines);

    /** Sets fixed line @p line (< staticLines). */
    void setStatic(size_t line, const std::string& text);

    /** Adds @p text as the newest log line. */
    void push(const std::string& text);

    /** Empties the log lines and keeps the fixed ones. */
    void clearLog();

    size_t size() const { return lines_.size(); }

    const std::string& line(size_t index) const { return lines_[index]; }

    /** @return a counter that changes whenever any line changes. */
    uint32_t version() const { return version_; }

private:
    size_t staticLines_;
    std::vector<std::string> lines_;
    uint32_t version_ = 0;
};
