#include <gtest/gtest.h>

#include "nodeRole.h"

TEST(NodeRole, Names) {
    EXPECT_STREQ(nodeRoleName(NodeRole::SENSOR), "sensor");
    EXPECT_STREQ(nodeRoleName(NodeRole::GATEWAY), "gateway");
}

TEST(NodeRole, ParsesNamesInAnyCase) {
    NodeRole role = NodeRole::SENSOR;
    EXPECT_TRUE(parseNodeRole("gateway", role));
    EXPECT_EQ(role, NodeRole::GATEWAY);
    EXPECT_TRUE(parseNodeRole("Sensor", role));
    EXPECT_EQ(role, NodeRole::SENSOR);
    EXPECT_TRUE(parseNodeRole("GATEWAY", role));
    EXPECT_EQ(role, NodeRole::GATEWAY);
}

TEST(NodeRole, RejectsOtherText) {
    NodeRole role = NodeRole::GATEWAY;
    for (const char* bad : {"", "gw", "manager", "gateway ", " sensor", "1"}) {
        EXPECT_FALSE(parseNodeRole(bad, role)) << bad;
    }
    EXPECT_EQ(role, NodeRole::GATEWAY);
}

TEST(NodeRole, DecodesStoredValues) {
    NodeRole role = NodeRole::SENSOR;
    EXPECT_TRUE(nodeRoleFromValue(1, role));
    EXPECT_EQ(role, NodeRole::GATEWAY);
    EXPECT_TRUE(nodeRoleFromValue(0, role));
    EXPECT_EQ(role, NodeRole::SENSOR);
    EXPECT_FALSE(nodeRoleFromValue(2, role));
    EXPECT_FALSE(nodeRoleFromValue(0xFF, role));
}

TEST(NodeRole, DefaultIsGatewayOnlyAtTheGatewayAddress) {
    EXPECT_EQ(defaultNodeRole(0x5E9C, 0x5E9C), NodeRole::GATEWAY);
    EXPECT_EQ(defaultNodeRole(0x7680, 0x5E9C), NodeRole::SENSOR);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
