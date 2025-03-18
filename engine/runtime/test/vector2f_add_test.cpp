#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(Vector2AddTest, AddTwoVectors)
{
    kpengine::Vector2f v1{1.f, 2.f};

    // case1
    {
        kpengine::Vector2f v2{3.f, 2.f};
        EXPECT_EQ(v1 + v2, kpengine::Vector2f(4.f, 4.f));
    }
    // case2
    {
        kpengine::Vector2f v2{0.f};
        EXPECT_EQ(v1 + v2, kpengine::Vector2f(1.f, 2.f));
    }
    // case3
    {
        kpengine::Vector2f v2{-4.f, 2.f};
        EXPECT_EQ(v1 + v2, kpengine::Vector2f(-3.f, 4.f));
    }
}

TEST(Vector2AddTest, AddScalarToVector)
{
    kpengine::Vector2f v1{1.f, 2.f};

    // case1
    {
        float scalar = 9.f;
        EXPECT_EQ(v1 + scalar, kpengine::Vector2f(10.f, 11.f));
        EXPECT_EQ(scalar + v1, kpengine::Vector2f(10.f, 11.f));
    }
    // case1
    {
        float scalar = 0.f;
        EXPECT_EQ(v1 + scalar, kpengine::Vector2f(1.f, 2.f));
        EXPECT_EQ(scalar + v1, kpengine::Vector2f(1.f, 2.f));
    }
    // case1
    {
        float scalar = -4.f;
        EXPECT_EQ(v1 + scalar, kpengine::Vector2f(-3.f, -2.f));
        EXPECT_EQ(scalar + v1, kpengine::Vector2f(-3.f, -2.f));
    }
}
