#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(Vector2SubTest, SubtractTwoVectors)
{
    kpengine::Vector2f v1{1.f, 2.f};

    // case1
    {
        kpengine::Vector2f v2{3.f, 2.f};
        EXPECT_EQ(v1 - v2, kpengine::Vector2f(-2.f, 0.f));
    }
    // case2
    {
        kpengine::Vector2f v2{0.f};
        EXPECT_EQ(v1 - v2, kpengine::Vector2f(1.f, 2.f));
    }
    // case3
    {
        kpengine::Vector2f v2{-4.f, 2.f};
        EXPECT_EQ(v1 - v2, kpengine::Vector2f(5.f, 0.f));
    }
}

TEST(Vector2SubTest, SubtractScalarFromVector)
{
    kpengine::Vector2f v1{1.f, 2.f};

    // case1
    {
        float scalar = 9.f;
        EXPECT_EQ(v1 - scalar, kpengine::Vector2f(-8.f, -7.f));
        EXPECT_EQ(scalar - v1, kpengine::Vector2f(8.f, 7.f));

    }
    // case2
    {
        float scalar = 0.f;
        EXPECT_EQ(v1 - scalar, kpengine::Vector2f(1.f, 2.f));
        EXPECT_EQ(scalar - v1, kpengine::Vector2f(-1.f, -2.f));

    }
    // case3
    {
        float scalar = -4.f;
        EXPECT_EQ(v1 - scalar, kpengine::Vector2f(5.f, 6.f));
        EXPECT_EQ(scalar - v1, kpengine::Vector2f(-5.f, -6.f));

    }
}
