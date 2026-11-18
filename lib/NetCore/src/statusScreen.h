#pragma once

#include <cstddef>
#include <cstdint>

/** Mesh protocol state, in the order of the LoRaMesher ProtocolState enum. */
enum class MeshState : uint8_t {
    INITIALIZING,
    DISCOVERY,
    JOINING,
    NORMAL_OPERATION,
    NETWORK_MANAGER,
    FAULT_RECOVERY,
    NM_ELECTION,
};

/** Characters of the header text. It starts at column 2, after the icon. */
constexpr size_t SCREEN_HEADER_CHARS = 19;
/** Characters of the big line, drawn at text size 2. */
constexpr size_t SCREEN_BIG_CHARS = 10;
/** Characters of a row at text size 1. */
constexpr size_t SCREEN_ROW_CHARS = 21;
/** Rows below the big line. */
constexpr size_t SCREEN_ROWS = 5;
/** Neighbours the screen has room for. */
constexpr size_t SCREEN_NEIGHBOURS = 3;
/** Seconds a /displayText line stays on the screen. */
constexpr uint32_t SCREEN_TEXT_SECONDS = 300;

/** A direct neighbour as the mesh last heard it. */
struct StatusNeighbour {
    uint16_t address = 0;
    /** False when the mesh has no signal reading of the neighbour yet. */
    bool signalKnown = false;
    int16_t rssiDbm = 0;
    float snrDb = 0.0F;
    uint32_t ageSeconds = 0;
};

/** State of the node at one instant. The firmware fills it once per second. */
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding): the fields are grouped by meaning
struct StatusSnapshot {
    bool gateway = false;
    uint16_t address = 0;
    MeshState meshState = MeshState::INITIALIZING;
    uint32_t stateSeconds = 0;
    /** Firmware version, NUL terminated. A "+g<hash>" build suffix is not shown. */
    char version[32] = {};

    /** False when the board has no PMU to read. */
    bool powerKnown = false;
    bool batteryPresent = false;
    bool externalPower = false;
    uint32_t batteryMv = 0;

    /** False until the node hears the mesh. Filled with statusNoteHeard(). */
    bool everHeard = false;
    /** Seconds since the node last heard anything from the mesh. */
    uint32_t lastHeardSeconds = 0;
    /** False while the node is not synchronised with the mesh. */
    bool syncKnown = false;

    /** A route to a gateway exists. */
    bool hasRoute = false;
    uint16_t nextHop = 0;
    /** Hops to that gateway, 0 when unknown. */
    uint8_t gatewayHops = 0;
    bool nextHopSnrKnown = false;
    float nextHopSnrDb = 0.0F;

    /** Best direct neighbours by RSSI, best first. Filled with statusAddNeighbour(). */
    StatusNeighbour neighbours[SCREEN_NEIGHBOURS];
    uint8_t neighbourCount = 0;
    /** Nodes the mesh knows. */
    uint32_t connectedNodes = 0;

    /** Seconds left of the install mode, 0 when it is off. */
    uint32_t installSeconds = 0;

    bool hasTemperature = false;
    float temperatureC = 0.0F;
    bool hasHumidity = false;
    float humidityPercent = 0.0F;
    bool hasPressure = false;
    float pressureHpa = 0.0F;

    bool wifiConnected = false;
    int16_t wifiRssiDbm = 0;
    bool mqttConnected = false;

    /** Last /displayText line, NUL terminated. Empty when there is none. */
    char text[SCREEN_ROW_CHARS + 1] = {};
    uint32_t textSeconds = 0;

    uint32_t uptimeSeconds = 0;
};

/** Icon drawn in the first column of the header. */
enum class ScreenIcon : uint8_t { JOINED, SEARCHING, JOINING, FAULT, GATEWAY, INSTALL };

/** Frames of the SEARCHING icon. */
constexpr uint8_t SCREEN_SEARCH_FRAMES = 4;

/**
 * @brief What the panel shows: an icon, a header, one big line and five rows.
 *
 * Every text is NUL terminated and never longer than its width. buildStatusScreen() sets every
 * byte, so two models are equal exactly when memcmp() says so.
 */
struct ScreenModel {
    ScreenIcon icon;
    /** Frame of the SEARCHING icon, 0 for the other icons. */
    uint8_t iconFrame;
    /** The header is drawn dark on a lit bar. */
    bool invertHeader;
    char header[SCREEN_HEADER_CHARS + 1];
    char big[SCREEN_BIG_CHARS + 1];
    char rows[SCREEN_ROWS][SCREEN_ROW_CHARS + 1];
};

/**
 * @brief Adds a direct neighbour to @p snapshot, keeping the SCREEN_NEIGHBOURS with the
 *        strongest RSSI in order. Neighbours without a signal reading come last.
 */
void statusAddNeighbour(StatusSnapshot& snapshot, const StatusNeighbour& neighbour);

/** Records that the node heard the mesh @p ageSeconds ago. The smallest age is kept. */
void statusNoteHeard(StatusSnapshot& snapshot, uint32_t ageSeconds);

/**
 * @brief Writes @p seconds as a number and one unit letter: 59s, 4m, 2h, 9d.
 * @param out at least 5 bytes.
 * @param maxDays largest number of days shown, at most 999.
 */
void formatAge(char* out, uint32_t seconds, uint32_t maxDays);

/** Lays out @p snapshot as one screen. */
void buildStatusScreen(const StatusSnapshot& snapshot, ScreenModel& out);
