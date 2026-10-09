#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

/** Kind of maintenance window opened by command. Persisted, never renumber. */
enum class WindowKind : uint8_t {
    NONE = 0,
    /** The node joins its WiFi network and checks the OTA server. */
    PULL = 1,
    /** The node opens its own access point for an upload. */
    AP = 2,
};

/** Outcome of a request to open a window. Values are reported, never renumber. */
enum class OpenDecision : uint8_t {
    OPENED = 0,
    /** A window of the same kind was open; it now lasts at least the requested time. */
    EXTENDED = 1,
    /** The node already opened MAX_PER_DAY windows in the last 24 h. */
    REFUSED_LIMIT = 2,
    /** Zero, or longer than the kind allows. */
    REFUSED_DURATION = 3,
};

/**
 * @brief Plan of the maintenance windows opened by command, on the node clock.
 *
 * The node clock counts seconds and is kept in NVS: it continues from the stored value after a
 * reboot and stands still while the node is off, so the 24 h limit only gets stricter. Windows
 * of the same kind extend each other without counting; a window of another kind replaces the
 * open one and counts. A window survives a reboot (it is resumed with its remaining time and
 * does not count again). Boot windows are not planned here and never count.
 */
class MaintWindowPlan {
public:
    static constexpr size_t MAX_PER_DAY = 8;
    static constexpr uint32_t DAY_S = 24 * 3600;
    static constexpr uint32_t MAX_PULL_S = 7200;
    static constexpr uint32_t MAX_AP_S = 1800;
    /** An access point closes this long after the result of an upload is known. */
    static constexpr uint32_t RESULT_GRACE_S = 120;
    /** The node clock is saved this often while a window is open... */
    static constexpr uint32_t SAVE_IN_WINDOW_S = 300;
    /** ...and this often while a counted window is less than 24 h old. */
    static constexpr uint32_t SAVE_WHILE_COUNTING_S = 3600;

    /** Opens, extends or refuses a window of @p durationS seconds at node time @p now. */
    OpenDecision open(WindowKind kind, uint32_t durationS, uint32_t now);

    /** Closes the open window. */
    void close() {
        kind_ = WindowKind::NONE;
        endsAt_ = 0;
    }

    /** The result of an upload is known: an open access point closes RESULT_GRACE_S later. */
    void onResult(uint32_t now);

    /** @return the kind of the window open at @p now, or NONE. */
    WindowKind active(uint32_t now) const;

    /** @return seconds left in the open window, 0 if none. */
    uint32_t remaining(uint32_t now) const;

    /** @return windows counted in the 24 h before @p now. */
    size_t countedInLastDay(uint32_t now) const;

    /** @return true if the node clock should be saved at @p now, last saved at @p savedAt. */
    bool clockSaveDue(uint32_t now, uint32_t savedAt) const;

    /** Versioned blob for NVS. Fields are only appended. */
    std::vector<uint8_t> encode() const;

    /** @return false if @p data is not a plan. */
    static bool decode(const uint8_t* data, size_t size, MaintWindowPlan& out);

    bool operator==(const MaintWindowPlan& other) const;

private:
    void recordOpen(uint32_t now);

    WindowKind kind_ = WindowKind::NONE;
    uint32_t endsAt_ = 0;
    /** Open times of counted windows, oldest first. */
    std::array<uint32_t, MAX_PER_DAY> history_{};
    uint8_t historyCount_ = 0;
};
