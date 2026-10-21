#include <managers/SeatManager.hpp>

#include <gtest/gtest.h>

// The seat manager is created during compositor init, so seat lookups can
// happen with no manager at all (unit tests, early startup). The null-safe
// accessors must degrade to "no seat" instead of dereferencing null, and
// must hand out the manager's real seats once one exists.

TEST(SeatManagerAccess, NoManagerYieldsNoSeat) {
    g_pSeatManager.reset();

    EXPECT_TRUE(SeatManager::seats().empty());
    EXPECT_FALSE(SeatManager::defaultSeat());
}

TEST(SeatManagerAccess, ManagerSeatsAreVisibleThroughAccessors) {
    g_pSeatManager = makeUnique<CSeatManager>();

    // the default seat is always present
    EXPECT_EQ(SeatManager::seats().size(), 1U);
    ASSERT_TRUE(SeatManager::defaultSeat());
    EXPECT_TRUE(SeatManager::defaultSeat()->isDefault());
    EXPECT_EQ(SeatManager::defaultSeat()->name(), DEFAULT_SEAT_NAME);

    // the accessors must return the manager's own list, not a copy
    EXPECT_EQ(&SeatManager::seats(), &g_pSeatManager->seats());
    EXPECT_EQ(SeatManager::defaultSeat(), g_pSeatManager->defaultSeat());

    g_pSeatManager.reset();
}
