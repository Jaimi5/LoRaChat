#pragma once

#include <cstdint>

/** What the routing table of the mesh says about one node. */
struct RouteState {
    /** The table has an entry for the node. */
    bool listed = false;
    /** The mesh uses the entry: the node, or its relay, was heard lately. */
    bool active = false;
    /** Hops to the node in the entry. */
    uint8_t hops = 0;
    /** Neighbour that relays toward the node in the entry. */
    uint16_t nextHop = 0;
};

/** The path a command takes to its node. */
struct CommandRoute {
    uint8_t hops = 0;
    uint16_t nextHop = 0;
};

/**
 * @brief Decides whether a command for node @p dst can be sent, and over which path.
 *
 * An active entry gives the path. A direct neighbour that went silent is still tried directly
 * while the mesh lists it, because it may only have missed some routing messages. A node the
 * mesh does not list, or a silent node behind a relay, has no route.
 *
 * @return false, leaving @p out unchanged, if there is no route. Nothing must be sent then.
 */
bool commandRoute(uint16_t dst, const RouteState& state, CommandRoute& out);
