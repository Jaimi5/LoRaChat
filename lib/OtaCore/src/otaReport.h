#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "otaRecord.h"

/** What a node report tells the server. The names are the server's schema v1 events. */
enum class ReportEvent : uint8_t {
    /** The server offers nothing to install (the policy decision says why). */
    NOOP,
    /** A new image was written and is about to boot. */
    WRITTEN,
    /** The new image passed its self-test. */
    VALID,
    /** The new image was rolled back. */
    ROLLBACK,
    /** A download or install failed; the running image stays. */
    FAILED,
};

/** @return the schema name of @p event. */
const char* reportEventName(ReportEvent event);

/**
 * @brief Identifies the verdict of an update attempt, so that it is reported once.
 *
 * The attempted image, its outcome and the count of unexplained rollbacks: each rollback of
 * the same image is a new verdict.
 */
std::vector<uint8_t> verdictId(const OtaRecord& record);

/**
 * @brief Writes the JSON body of a node report for POST /report (server schema v1).
 *
 * Fields are written in the order they are added, after {"v":1. Text values must be 1-48
 * printable ASCII characters without quotes or backslashes, as the server requires; any other
 * value makes the whole report invalid.
 */
class ReportBuilder {
public:
    static constexpr size_t MAX_SIZE = 1024;
    static constexpr size_t MAX_TEXT = 48;

    ReportBuilder();

    ReportBuilder& addText(const char* name, const std::string& value);
    ReportBuilder& addNumber(const char* name, int64_t value);
    ReportBuilder& addFlag(const char* name, bool value);

    /** @return the body, or an empty string if a value was invalid or it exceeds MAX_SIZE. */
    std::string body() const;

private:
    void key(const char* name);

    std::string json_;
    bool valid_ = true;
};
