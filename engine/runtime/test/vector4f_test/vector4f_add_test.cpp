#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(Vector4AddTest, AddTwoVectors)
{
    kpengine::Vector4f v1{1.f, 2.f, 3.f, 4.f};

    // case1
    {
        kpengine::Vector4f v2{3.f, 2.f, 1.f, 0.f};
        EXPECT_EQ(v1 + v2, kpengine::Vector4f(4.f, 4.f, 4.f, 4.f));
    }
    // case2
    {
        kpengine::Vector4f v2{0.f};
        EXPECT_EQ(v1 + v2, kpengine::Vector4f(1.f, 2.f, 3.f, 4.f));
    }
    // case3
    {
        kpengine::Vector4f v2{-4.f, 2.f, 0.f, -1.f};
        EXPECT_EQ(v1 + v2, kpengine::Vector4f(-3.f, 4.f, 3.f, 3.f));
    }
}

TEST(Vector4AddTest, AddScalarToVector)
{
    kpengine::Vector4f v1{1.f, 2.f, 3.f, 4.f};

    // case1
    {
        float scalar = 9.f;
        EXPECT_EQ(v1 + scalar, kpengine::Vector4f(10.f, 11.f, 12.f, 13.f));
        EXPECT_EQ(scalar + v1, kpengine::Vector4f(10.f, 11.f, 12.f, 13.f));
    }
    // case2
    {
        float scalar = 0.f;
        EXPECT_EQ(v1 + scalar, kpengine::Vector4f(1.f, 2.f, 3.f, 4.f));
        EXPECT_EQ(scalar + v1, kpengine::Vector4f(1.f, 2.f, 3.f, 4.f));
    }
    // case3
    {
        float scalar = -4.f;
        EXPECT_EQ(v1 + scalar, kpengine::Vector4f(-3.f, -2.f, -1.f, 0.f));
        EXPECT_EQ(scalar + v1, kpengine::Vector4f(-3.f, -2.f, -1.f, 0.f));
    }
}