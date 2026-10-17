#include "displayLines.h"

DisplayLines::DisplayLines(size_t staticLines, size_t totalLines)
    : staticLines_(staticLines), lines_(totalLines) {}

void DisplayLines::setStatic(size_t line, const std::string& text) {
    if (line >= staticLines_ || line >= lines_.size()) return;
    lines_[line] = text;
    version_++;
}

void DisplayLines::push(const std::string& text) {
    if (staticLines_ >= lines_.size()) return;
    for (size_t i = lines_.size() - 1; i > staticLines_; --i) lines_[i] = lines_[i - 1];
    lines_[staticLines_] = text;
    version_++;
}

void DisplayLines::clearLog() {
    for (size_t i = staticLines_; i < lines_.size(); i++) lines_[i].clear();
    version_++;
}
