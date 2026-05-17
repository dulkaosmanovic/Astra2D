#include <gtest/gtest.h>

// Simple function to test
int Add(int a, int b)
{
    return a + b;
}

// Test case
TEST(MathTests, AdditionWorks)
{
    EXPECT_EQ(Add(2, 3), 5);
    EXPECT_EQ(Add(-1, 1), 0);
    EXPECT_EQ(Add(0, 0), 0);
}
