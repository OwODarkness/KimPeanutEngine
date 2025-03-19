#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(Vector4DivTest, DivideVectorByScalar)
{
    kpengine::Vector4f v1{1.f, 2.f, 3.f, 4.f};

    // case1
    {
        float scalar = 2.f;
        EXPECT_EQ(v1 / scalar, kpengine::Vector4f(0.5f, 1.f, 1.5f, 2.f));
    }
    // case2
    {
        float scalar = -1.f;
        EXPECT_EQ(v1 / scalar, kpengine::Vector4f(-1.f, -2.f, -3.f, -4.f));
    }
    // case3
    {
        float scalar = 0.5f;
        EXPECT_EQ(v1 / scalar, kpengine::Vector4f(2.f, 4.f, 6.f, 8.f));
    }
}
