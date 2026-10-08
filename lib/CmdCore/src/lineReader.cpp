#include "lineReader.h"

constexpr size_t LineReader::MAX_LINE;

bool LineReader::push(char byte, std::string& line) {
    if (byte != '\r' && byte != '\n') {
        if (buffer_.size() < MAX_LINE) {
            buffer_.push_back(byte);
        } else {
            overflow_ = true;
        }
        return false;
    }

    bool dropped = overflow_;
    std::string raw;
    raw.swap(buffer_);
    overflow_ = false;
    if (dropped) return false;

    size_t first = raw.find_first_not_of(' ');
    if (first == std::string::npos) return false;
    size_t last = raw.find_last_not_of(' ');
    line = raw.substr(first, last - first + 1);
    return true;
}
