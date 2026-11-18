#include <gtest/gtest.h>

#include <cstdint>

#include "cmdRoute.h"

namespace {

constexpr uint16_t NODE = 0x7680;
constexpr uint16_t RELAY = 0x5E9C;

RouteState entry(bool active, uint8_t hops, uint16_t nextHop) {
    RouteState state;
    state.listed = true;
    state.active = active;
    state.hops = hops;
    state.nextHop = nextHop;
    return state;
}

}  // namespace

TEST(CommandRoute, ANodeTheMeshDoesNotListHasNoRoute) {
    CommandRoute route;
    route.hops = 9;
    EXPECT_FALSE(commandRoute(0x1234, RouteState{}, route));
    EXPECT_EQ(route.hops, 9);
}

TEST(CommandRoute, AnActiveEntryGivesItsHopsAndNextHop) {
    CommandRoute route;
    ASSERT_TRUE(commandRoute(NODE, entry(true, 1, NODE), route));
    EXPECT_EQ(route.hops, 1);
    EXPECT_EQ(route.nextHop, NODE);

    ASSERT_TRUE(commandRoute(NODE, entry(true, 3, RELAY), route));
    EXPECT_EQ(route.hops, 3);
    EXPECT_EQ(route.nextHop, RELAY);
}

TEST(CommandRoute, ASilentNeighbourStillListedIsTriedDirectly) {
    CommandRoute route;
    ASSERT_TRUE(commandRoute(NODE, entry(false, 1, NODE), route));
    EXPECT_EQ(route.hops, 1);
    EXPECT_EQ(route.nextHop, NODE);

    // The next hop of an entry the mesh does not use is not trusted.
    ASSERT_TRUE(commandRoute(NODE, entry(false, 1, 0), route));
    EXPECT_EQ(route.nextHop, NODE);
}

TEST(CommandRoute, ASilentNodeBehindARelayHasNoRoute) {
    CommandRoute route;
    EXPECT_FALSE(commandRoute(NODE, entry(false, 2, RELAY), route));
    EXPECT_FALSE(commandRoute(NODE, entry(false, 5, RELAY), route));
}

TEST(CommandRoute, AnEntryWithoutHopsOrNextHopHasNoRoute) {
    CommandRoute route;
    EXPECT_FALSE(commandRoute(NODE, entry(true, 0, NODE), route));
    EXPECT_FALSE(commandRoute(NODE, entry(true, 2, 0), route));
    EXPECT_FALSE(commandRoute(NODE, entry(false, 0, NODE), route));
}

TEST(CommandRoute, AnUnlistedNodeHasNoRouteWhateverTheOtherFieldsSay) {
    RouteState state = entry(true, 1, NODE);
    state.listed = false;
    CommandRoute route;
    EXPECT_FALSE(commandRoute(NODE, state, route));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
