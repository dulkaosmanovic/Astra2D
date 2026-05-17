#include <gtest/gtest.h>

// Simple function to test
int Sub(int a, int b)
{
    return a - b;
}

// Test case
TEST(CollisonTests, SubWorks)
{
    EXPECT_EQ(Sub(2, 3), -1);
    EXPECT_EQ(Sub(-1, 1), -2);
    EXPECT_EQ(Sub(0, 0), 0);
}
