#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(VectorSubTest, SubtractTwoVectors)
{
    kpengine::Vector3f v1{1.f, 2.f, 3.f};

    // case1
    {
        kpengine::Vector3f v2{3.f, 2.f, 1.f};
        EXPECT_EQ(v1 - v2, kpengine::Vector3f(-2.f, 0.f, 2.f));
    }
    // case2
    {
        kpengine::Vector3f v2{0.f};
        EXPECT_EQ(v1 - v2, kpengine::Vector3f(1.f, 2.f, 3.f));
    }
    // case3
    {
        kpengine::Vector3f v2{-4.f, 2.f, 0.f};
        EXPECT_EQ(v1 - v2, kpengine::Vector3f(5.f, 0.f, 3.f));
    }
}

TEST(VectorSubTest, SubtractScalarFromVector)
{
    kpengine::Vector3f v1{1.f, 2.f, 3.f};

    // case1
    {
        float scalar = 9.f;
        EXPECT_EQ(v1 - scalar, kpengine::Vector3f(-8.f, -7.f, -6.f));
    }
    // case2
    {
        float scalar = 0.f;
        EXPECT_EQ(v1 - scalar, kpengine::Vector3f(1.f, 2.f, 3.f));
    }
    // case3
    {
        float scalar = -4.f;
        EXPECT_EQ(v1 - scalar, kpengine::Vector3f(5.f, 6.f, 7.f));
    }
}
