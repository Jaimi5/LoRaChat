#include "cmdRoute.h"

bool commandRoute(uint16_t dst, const RouteState& state, CommandRoute& out) {
    if (!state.listed || state.hops == 0) return false;
    if (state.active) {
        if (state.nextHop == 0) return false;
        out.hops = state.hops;
        out.nextHop = state.nextHop;
        return true;
    }
    if (state.hops != 1) return false;
    out.hops = 1;
    out.nextHop = dst;
    return true;
}
