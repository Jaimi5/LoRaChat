#include "statusScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "linkQuality.h"

namespace {

constexpr uint32_t MINUTE_SECONDS = 60;
constexpr uint32_t HOUR_SECONDS = 3600;
constexpr uint32_t DAY_SECONDS = 86400;
/** Largest number of days in the fields that hold three characters. */
constexpr uint32_t SHORT_AGE_DAYS = 99;
/** Characters of the version shown on the last row. */
constexpr size_t VERSION_CHARS = 10;

bool isFault(MeshState state) {
    return state == MeshState::FAULT_RECOVERY || state == MeshState::NM_ELECTION;
}

bool isSearching(MeshState state) {
    return state == MeshState::INITIALIZING || state == MeshState::DISCOVERY;
}

const char* stateWord(MeshState state) {
    switch (state) {
        case MeshState::INITIALIZING:
            return "INIT";
        case MeshState::DISCOVERY:
            return "DISCOVER";
        case MeshState::JOINING:
            return "JOINING";
        case MeshState::NORMAL_OPERATION:
            return "JOINED";
        case MeshState::NETWORK_MANAGER:
            return "MANAGER";
        case MeshState::FAULT_RECOVERY:
            return "RECOVER";
        case MeshState::NM_ELECTION:
            return "ELECTION";
    }
    return "?";
}

/** Copies at most @p width printable characters of @p text to @p out and terminates it. */
void putText(char* out, size_t width, const char* text) {
    size_t length = 0;
    for (; length < width && text[length] != '\0'; length++) {
        char c = text[length];
        out[length] = (c >= ' ' && c <= '~') ? c : '?';
    }
    out[length] = '\0';
}

/** @return true if @p left and @p right fit in @p width characters with a space between. */
bool fits(const char* left, const char* right, size_t width) {
    size_t rightLength = std::strlen(right);
    return std::strlen(left) + (rightLength > 0 ? rightLength + 1 : 0) <= width;
}

/**
 * Writes @p left at the start and @p right at the end of a row of @p width characters. The
 * right side is kept and the left side is cut, leaving @p gap spaces before the right side.
 */
void joinRow(char* out, size_t width, const char* left, const char* right, size_t gap) {
    size_t rightLength = std::min(std::strlen(right), width);
    if (rightLength == 0) {
        putText(out, width, left);
        return;
    }
    size_t room = width - rightLength;
    size_t leftLength = std::strlen(left);
    if (leftLength > room - std::min(gap, room)) leftLength = room - std::min(gap, room);
    std::memset(out, ' ', width);
    putText(out, leftLength, left);
    out[std::min(leftLength, room)] = ' ';
    putText(out + room, rightLength, right);
}

/** @return @p value in tenths, limited to three digits. */
int toTenths(float value) {
    constexpr float LIMIT = 99.9F;
    if (!(value > -LIMIT)) return -999;
    if (value > LIMIT) return 999;
    return static_cast<int>(std::lround(value * 10.0F));
}

/** Writes tenths as a signed number with one decimal, at most 5 characters. */
void formatSigned(char* out, size_t size, int tenths) {
    int magnitude = tenths < 0 ? -tenths : tenths;
    std::snprintf(out, size, "%c%d.%d", tenths < 0 ? '-' : '+', magnitude / 10, magnitude % 10);
}

int clampDbm(int dbm) {
    return std::max(-199, std::min(dbm, 99));
}

void formatPower(char* out, size_t size, const StatusSnapshot& s) {
    if (s.powerKnown && s.batteryPresent) {
        unsigned centivolts =
            static_cast<unsigned>((std::min<uint32_t>(s.batteryMv, 9990) + 5) / 10);
        std::snprintf(out, size, "%s%u.%02uV", s.externalPower ? "+" : "", centivolts / 100,
                      centivolts % 100);
    } else if (s.powerKnown && s.externalPower) {
        std::snprintf(out, size, "USB");
    } else {
        std::snprintf(out, size, "--");
    }
}

void formatLastHeard(char* out, size_t size, const StatusSnapshot& s) {
    if (s.everHeard) {
        formatAge(out, s.lastHeardSeconds, SHORT_AGE_DAYS);
    } else {
        std::snprintf(out, size, "--");
    }
}

/** @return the verdict of the link to the next hop, or of the best neighbour without one. */
Verdict linkVerdict(const StatusSnapshot& s) {
    if (s.hasRoute && s.nextHopSnrKnown) return verdictFromSnr(true, s.nextHopSnrDb);
    bool known = false;
    float best = 0.0F;
    for (size_t i = 0; i < s.neighbourCount && i < SCREEN_NEIGHBOURS; i++) {
        const StatusNeighbour& neighbour = s.neighbours[i];
        if (!neighbour.signalKnown || (known && neighbour.snrDb <= best)) continue;
        known = true;
        best = neighbour.snrDb;
    }
    return verdictFromSnr(known, best);
}

ScreenIcon iconFor(const StatusSnapshot& s) {
    if (isFault(s.meshState)) return ScreenIcon::FAULT;
    if (s.installSeconds > 0) return ScreenIcon::INSTALL;
    if (s.gateway) return ScreenIcon::GATEWAY;
    if (isSearching(s.meshState)) return ScreenIcon::SEARCHING;
    if (s.meshState == MeshState::JOINING) return ScreenIcon::JOINING;
    return ScreenIcon::JOINED;
}

void buildHeader(const StatusSnapshot& s, char* out) {
    const char* word = stateWord(s.meshState);
    bool healthy =
        s.meshState == MeshState::NORMAL_OPERATION || s.meshState == MeshState::NETWORK_MANAGER;
    if (s.gateway && healthy) word = "GATEWAY";
    if (s.installSeconds > 0 && !isFault(s.meshState)) word = "INSTALL";

    char left[48];
    std::snprintf(left, sizeof(left), "%04X %s", static_cast<unsigned>(s.address), word);
    char power[24];
    formatPower(power, sizeof(power), s);
    joinRow(out, SCREEN_HEADER_CHARS, left, power, 0);
}

void buildBigLine(const StatusSnapshot& s, char* out) {
    char right[8] = "";
    const char* left;
    if (s.gateway) {
        // A gateway fault shows its word alone.
        if (!s.wifiConnected) {
            left = "NO WIFI";
        } else if (!s.mqttConnected) {
            left = "NO MQTT";
        } else {
            left = "MQTT";
            formatLastHeard(right, sizeof(right), s);
        }
    } else if (isFault(s.meshState)) {
        left = stateWord(s.meshState);
    } else if (isSearching(s.meshState) || s.meshState == MeshState::JOINING) {
        left = isSearching(s.meshState) ? "SEARCH" : "JOIN";
        formatAge(right, s.stateSeconds, SHORT_AGE_DAYS);
    } else {
        left = verdictName(linkVerdict(s));
        formatLastHeard(right, sizeof(right), s);
    }
    joinRow(out, SCREEN_BIG_CHARS, left, right, 1);
}

/** Writes the hops to the gateway in the widest form that leaves @p room characters free. */
void formatHops(char* out, size_t size, unsigned hops, size_t room) {
    std::snprintf(out, size, "%u %s", hops, hops == 1 ? "hop" : "hops");
    if (std::strlen(out) > room) std::snprintf(out, size, "%u%s", hops, hops == 1 ? "hop" : "hops");
}

void buildRouteRow(const StatusSnapshot& s, char* out) {
    char left[48];
    char shortLeft[48];
    if (!s.hasRoute) {
        std::snprintf(left, sizeof(left), "%s", s.syncKnown ? "no route" : "no sync yet");
        std::snprintf(shortLeft, sizeof(shortLeft), "%s", left);
    } else if (!s.nextHopSnrKnown) {
        std::snprintf(left, sizeof(left), "via %04X --", static_cast<unsigned>(s.nextHop));
        std::snprintf(shortLeft, sizeof(shortLeft), "%s", left);
    } else {
        char snr[24];
        formatSigned(snr, sizeof(snr), toTenths(s.nextHopSnrDb));
        std::snprintf(left, sizeof(left), "via %04X %sdB", static_cast<unsigned>(s.nextHop), snr);
        std::snprintf(shortLeft, sizeof(shortLeft), "via %04X %s", static_cast<unsigned>(s.nextHop),
                      snr);
    }

    char right[24] = "";
    if (s.installSeconds > 0) {
        unsigned minutes = static_cast<unsigned>(std::min<uint32_t>(s.installSeconds / 60, 99));
        std::snprintf(right, sizeof(right), "%u:%02u", minutes,
                      static_cast<unsigned>(s.installSeconds % 60));
    } else if (s.hasRoute && s.gatewayHops > 0) {
        size_t taken = std::strlen(shortLeft) + 1;
        formatHops(right, sizeof(right), s.gatewayHops,
                   SCREEN_ROW_CHARS - std::min(taken, SCREEN_ROW_CHARS));
    }
    // The unit goes first when the row is full.
    joinRow(out, SCREEN_ROW_CHARS, fits(left, right, SCREEN_ROW_CHARS) ? left : shortLeft, right,
            1);
}

void buildGatewayRow(const StatusSnapshot& s, char* out) {
    char left[48];
    if (s.wifiConnected) {
        std::snprintf(left, sizeof(left), "wifi %ddBm", clampDbm(s.wifiRssiDbm));
    } else {
        std::snprintf(left, sizeof(left), "wifi --");
    }
    char right[24];
    std::snprintf(right, sizeof(right), "nodes %2u",
                  static_cast<unsigned>(std::min<uint32_t>(s.connectedNodes, 999)));
    joinRow(out, SCREEN_ROW_CHARS, left, right, 1);
}

void buildNeighbourRow(const StatusNeighbour& neighbour, char* out) {
    char rssi[16] = "--";
    char snr[24] = "--";
    if (neighbour.signalKnown) {
        std::snprintf(rssi, sizeof(rssi), "%d", clampDbm(neighbour.rssiDbm));
        formatSigned(snr, sizeof(snr), toTenths(neighbour.snrDb));
    }
    char age[5];
    formatAge(age, neighbour.ageSeconds, SHORT_AGE_DAYS);
    char row[80];
    std::snprintf(row, sizeof(row), "%04X %4s%s%5s%4s", static_cast<unsigned>(neighbour.address),
                  rssi, neighbour.signalKnown ? "dBm" : "   ", snr, age);
    putText(out, SCREEN_ROW_CHARS, row);
}

/** Adds @p piece to @p line after a space if the result has at most @p room characters. */
void appendPiece(char* line, size_t room, const char* piece) {
    size_t length = std::strlen(line);
    size_t pieceLength = std::strlen(piece);
    size_t space = length > 0 ? 1 : 0;
    if (pieceLength == 0 || length + space + pieceLength > room) return;
    if (space > 0) line[length] = ' ';
    std::memcpy(line + length + space, piece, pieceLength + 1);
}

void formatReading(const StatusSnapshot& s, char* out, size_t room) {
    out[0] = '\0';
    char piece[24];
    if (s.hasTemperature) {
        int tenths = toTenths(s.temperatureC);
        int magnitude = tenths < 0 ? -tenths : tenths;
        std::snprintf(piece, sizeof(piece), "%s%d.%dC", tenths < 0 ? "-" : "", magnitude / 10,
                      magnitude % 10);
        appendPiece(out, room, piece);
    }
    if (s.hasHumidity) {
        float humidity = std::max(0.0F, std::min(s.humidityPercent, 100.0F));
        if (!(humidity >= 0.0F)) humidity = 0.0F;
        std::snprintf(piece, sizeof(piece), "%ld%%RH", std::lround(humidity));
        appendPiece(out, room, piece);
    }
    if (s.hasPressure) {
        float pressure = std::max(0.0F, std::min(s.pressureHpa, 9999.0F));
        if (!(pressure >= 0.0F)) pressure = 0.0F;
        std::snprintf(piece, sizeof(piece), "%ldhPa", std::lround(pressure));
        appendPiece(out, room, piece);
    }
}

/** Writes the version as "v1.2.3", without the build suffix that follows a '+'. */
void formatVersion(const StatusSnapshot& s, char* out) {
    char version[sizeof(s.version) + 2];
    size_t length = 0;
    if (s.version[0] != '\0' && s.version[0] != 'v') version[length++] = 'v';
    for (size_t i = 0; i < sizeof(s.version) && s.version[i] != '\0' && s.version[i] != '+'; i++) {
        version[length++] = s.version[i];
    }
    version[length] = '\0';
    putText(out, VERSION_CHARS, version);
}

void buildLastRow(const StatusSnapshot& s, char* out) {
    char version[VERSION_CHARS + 1];
    formatVersion(s, version);
    size_t versionLength = std::strlen(version);
    size_t room = SCREEN_ROW_CHARS - (versionLength > 0 ? versionLength + 1 : 0);

    char left[SCREEN_ROW_CHARS + 1] = "";
    if (s.text[0] != '\0' && s.textSeconds < SCREEN_TEXT_SECONDS) {
        putText(left, std::min(room, sizeof(s.text) - 1), s.text);
    } else {
        formatReading(s, left, room);
    }
    if (left[0] == '\0') {
        char age[5];
        formatAge(age, s.uptimeSeconds, 999);
        std::snprintf(left, sizeof(left), "up %s", age);
    }
    joinRow(out, SCREEN_ROW_CHARS, left, version, 1);
}

/** @return true if @p a goes before @p b on the screen. */
bool strongerThan(const StatusNeighbour& a, const StatusNeighbour& b) {
    if (a.signalKnown != b.signalKnown) return a.signalKnown;
    if (!a.signalKnown) return a.ageSeconds < b.ageSeconds;
    return a.rssiDbm > b.rssiDbm;
}

}  // namespace

void statusAddNeighbour(StatusSnapshot& snapshot, const StatusNeighbour& neighbour) {
    size_t count = std::min<size_t>(snapshot.neighbourCount, SCREEN_NEIGHBOURS);
    size_t position = 0;
    while (position < count && !strongerThan(neighbour, snapshot.neighbours[position])) position++;
    if (position >= SCREEN_NEIGHBOURS) return;
    if (count < SCREEN_NEIGHBOURS) count++;
    for (size_t i = count - 1; i > position; i--)
        snapshot.neighbours[i] = snapshot.neighbours[i - 1];
    snapshot.neighbours[position] = neighbour;
    snapshot.neighbourCount = static_cast<uint8_t>(count);
}

void statusNoteHeard(StatusSnapshot& snapshot, uint32_t ageSeconds) {
    if (snapshot.everHeard && snapshot.lastHeardSeconds <= ageSeconds) return;
    snapshot.everHeard = true;
    snapshot.lastHeardSeconds = ageSeconds;
}

void formatAge(char* out, uint32_t seconds, uint32_t maxDays) {
    unsigned value = static_cast<unsigned>(seconds);
    char unit = 's';
    if (seconds >= DAY_SECONDS) {
        value = static_cast<unsigned>(
            std::min(seconds / DAY_SECONDS, std::min<uint32_t>(maxDays, 999)));
        unit = 'd';
    } else if (seconds >= HOUR_SECONDS) {
        value = static_cast<unsigned>(seconds / HOUR_SECONDS);
        unit = 'h';
    } else if (seconds >= MINUTE_SECONDS) {
        value = static_cast<unsigned>(seconds / MINUTE_SECONDS);
        unit = 'm';
    }
    std::snprintf(out, 5, "%u%c", value % 1000, unit);
}

void buildStatusScreen(const StatusSnapshot& snapshot, ScreenModel& out) {
    std::memset(&out, 0, sizeof(out));
    out.icon = iconFor(snapshot);
    if (out.icon == ScreenIcon::SEARCHING) {
        out.iconFrame = static_cast<uint8_t>(snapshot.uptimeSeconds % SCREEN_SEARCH_FRAMES);
    }
    out.invertHeader = isFault(snapshot.meshState);

    buildHeader(snapshot, out.header);
    buildBigLine(snapshot, out.big);
    if (snapshot.gateway) {
        buildGatewayRow(snapshot, out.rows[0]);
    } else {
        buildRouteRow(snapshot, out.rows[0]);
    }
    for (size_t i = 0; i < snapshot.neighbourCount && i < SCREEN_NEIGHBOURS; i++) {
        buildNeighbourRow(snapshot.neighbours[i], out.rows[1 + i]);
    }
    buildLastRow(snapshot, out.rows[SCREEN_ROWS - 1]);
}
