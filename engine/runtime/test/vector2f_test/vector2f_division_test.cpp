#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(Vector2DivisionTest, DivideVectorByScalar)
{
    kpengine::Vector2f v1{10.f, 20.f};

    // case1: Divide by a positive scalar
    {
        float scalar = 2.f;
        EXPECT_EQ(v1 / scalar, kpengine::Vector2f(5.f, 10.f));
    }
    // case2: Divide by 1 (should return the same vector)
    {
        float scalar = 1.f;
        EXPECT_EQ(v1 / scalar, kpengine::Vector2f(10.f, 20.f));
    }
    // case3: Divide by a negative scalar
    {
        float scalar = -2.f;
        EXPECT_EQ(v1 / scalar, kpengine::Vector2f(-5.f, -10.f));
    }
    // case4: Divide by a fractional scalar
    {
        float scalar = 0.5f;
        EXPECT_EQ(v1 / scalar, kpengine::Vector2f(20.f, 40.f));
    }
}
