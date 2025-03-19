#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(Vector4MulTest, MultiplyTwoVectors)
{
    kpengine::Vector4f v1{1.f, 2.f, 3.f, 4.f};

    // case1
    {
        kpengine::Vector4f v2{3.f, 2.f, 1.f, 0.f};
        EXPECT_EQ(v1 * v2, kpengine::Vector4f(3.f, 4.f, 3.f, 0.f));
    }
    // case2
    {
        kpengine::Vector4f v2{1.f};
        EXPECT_EQ(v1 * v2, kpengine::Vector4f(1.f, 2.f, 3.f, 4.f));
    }
    // case3
    {
        kpengine::Vector4f v2{-1.f, 2.f, -3.f, 4.f};
        EXPECT_EQ(v1 * v2, kpengine::Vector4f(-1.f, 4.f, -9.f, 16.f));
    }
}

TEST(Vector4MulTest, MultiplyVectorByScalar)
{
    kpengine::Vector4f v1{1.f, 2.f, 3.f, 4.f};

    // case1
    {
        float scalar = 2.f;
        EXPECT_EQ(v1 * scalar, kpengine::Vector4f(2.f, 4.f, 6.f, 8.f));
        EXPECT_EQ(scalar * v1, kpengine::Vector4f(2.f, 4.f, 6.f, 8.f));
    }
    // case2
    {
        float scalar = 0.f;
        EXPECT_EQ(v1 * scalar, kpengine::Vector4f(0.f, 0.f, 0.f, 0.f));
        EXPECT_EQ(scalar * v1, kpengine::Vector4f(0.f, 0.f, 0.f, 0.f));
    }
    // case3
    {
        float scalar = -1.f;
        EXPECT_EQ(v1 * scalar, kpengine::Vector4f(-1.f, -2.f, -3.f, -4.f));
        EXPECT_EQ(scalar * v1, kpengine::Vector4f(-1.f, -2.f, -3.f, -4.f));
    }
}

TEST(Vector4MultipleTest, DotProduct)
{
    // Case 1: Dot product of perpendicular vectors (should be 0)
    {
        kpengine::Vector4f v1{1.f, 0.f, 0.f, 0.f};
        kpengine::Vector4f v2{0.f, 1.f, 0.f, 0.f};
        EXPECT_FLOAT_EQ(v1.DotProduct(v2), 0.f);
    }

    // Case 2: Dot product of parallel vectors
    {
        kpengine::Vector4f v1{1.f, 2.f, 3.f, 4.f};
        kpengine::Vector4f v2{2.f, 4.f, 6.f, 8.f};
        EXPECT_FLOAT_EQ(v1.DotProduct(v2), 1.f * 2.f + 2.f * 4.f + 3.f * 6.f + 4.f * 8.f);
    }

    // Case 3: Dot product of opposite vectors
    {
        kpengine::Vector4f v1{1.f, -2.f, 3.f, 4.f};
        kpengine::Vector4f v2{-1.f, 2.f, -3.f, 5.f};
        EXPECT_FLOAT_EQ(v1.DotProduct(v2), -1.f - 4.f - 9.f + 20.f);
    }

    // Case 4: Dot product with itself (should be length squared)
    {
        kpengine::Vector4f v1{2.f, 3.f, 4.f, 5.f};
        EXPECT_FLOAT_EQ(v1.DotProduct(v1), 2.f * 2.f + 3.f * 3.f + 4.f * 4.f + 5.f * 5.f);
    } 
}

