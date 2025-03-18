#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(VectorAddTest, AddTwoVectors)
{
    kpengine::Vector3f v1{1.f, 2.f, 3.f};

    // case1
    {
        kpengine::Vector3f v2{3.f, 2.f, 1.f};
        EXPECT_EQ(v1 + v2, kpengine::Vector3f(4.f, 4.f, 4.f));
    }
    // case2
    {
        kpengine::Vector3f v2{0.f};
        EXPECT_EQ(v1 + v2, kpengine::Vector3f(1.f, 2.f, 3.f));
    }
    // case3
    {
        kpengine::Vector3f v2{-4.f, 2.f, 0.f};
        EXPECT_EQ(v1 + v2, kpengine::Vector3f(-3.f, 4.f, 3.f));
    }
}

TEST(VectorAddTest, AddScalarToVector)
{
    kpengine::Vector3f v1{1.f, 2.f, 3.f};

    // case1
    {
        float scalar = 9.f;
        EXPECT_EQ(v1 + scalar, kpengine::Vector3f(10.f, 11.f, 12.f));
        EXPECT_EQ(scalar + v1, kpengine::Vector3f(10.f, 11.f, 12.f));
    }
    // case1
    {
        float scalar = 0.f;
        EXPECT_EQ(v1 + scalar, kpengine::Vector3f(1.f, 2.f, 3.f));
        EXPECT_EQ(scalar + v1, kpengine::Vector3f(1.f, 2.f, 3.f));
    }
    // case1
    {
        float scalar = -4.f;
        EXPECT_EQ(v1 + scalar, kpengine::Vector3f(-3.f, -2.f, -1.f));
        EXPECT_EQ(scalar + v1, kpengine::Vector3f(-3.f, -2.f, -1.f));
    }
}
