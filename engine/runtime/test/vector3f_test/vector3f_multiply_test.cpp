#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

//Test multiply operation, dotproduct and cross product


TEST(Vector3MultiplyTest, MultipleTwoVectors)
{
    kpengine::Vector3f v1{1.f, 2.f, 3.f};

    //case1 
    {
        kpengine::Vector3f v2{1.f, 2.f, 3.f};
        EXPECT_EQ(v1 * v2, kpengine::Vector3f(1.f, 4.f, 9.f));
    }
    //case2
    {
        kpengine::Vector3f v2{0.f};
        EXPECT_EQ(v1 * v2, kpengine::Vector3f(0.f));
    }
    //case3
    {
        kpengine::Vector3f v2{1.f, -2.f, -3.f};
        EXPECT_EQ(v1 * v2, kpengine::Vector3f(1.f, -4.f, -9.f));
    }
}

TEST(Vector3MultipleTest, MultipleScalarToVector)
{
    kpengine::Vector3f v{1.f, -2.f, 3.f};

    // Case 1: Multiply by a positive scalar
    {
        float scalar = 2.f;
        EXPECT_EQ(scalar * v, kpengine::Vector3f(2.f, -4.f, 6.f));
        EXPECT_EQ(v * scalar, kpengine::Vector3f(2.f, -4.f, 6.f));
    }

    // Case 2: Multiply by zero
    {
        float scalar = 0.f;
        EXPECT_EQ(scalar * v, kpengine::Vector3f(0.f, 0.f, 0.f));
        EXPECT_EQ(v * scalar, kpengine::Vector3f(0.f, 0.f, 0.f));
    }

    // Case 3: Multiply by a negative scalar
    {
        float scalar = -1.f;
        EXPECT_EQ(scalar * v, kpengine::Vector3f(-1.f, 2.f, -3.f));
        EXPECT_EQ(v * scalar, kpengine::Vector3f(-1.f, 2.f, -3.f));
    }

    // Case 4: Multiply by a fractional scalar
    {
        float scalar = 0.5f;
        EXPECT_EQ(scalar * v, kpengine::Vector3f(0.5f, -1.f, 1.5f));
        EXPECT_EQ(v * scalar, kpengine::Vector3f(0.5f, -1.f, 1.5f));
    }
}

TEST(Vector3MultipleTest, DotProduct)
{
    // Case 1: Dot product of perpendicular vectors (should be 0)
    {
        kpengine::Vector3f v1{1.f, 0.f, 0.f};
        kpengine::Vector3f v2{0.f, 1.f, 0.f};
        EXPECT_FLOAT_EQ(v1.DotProduct(v2), 0.f);
    }

    // Case 2: Dot product of parallel vectors
    {
        kpengine::Vector3f v1{1.f, 2.f, 3.f};
        kpengine::Vector3f v2{2.f, 4.f, 6.f};
        EXPECT_FLOAT_EQ(v1.DotProduct(v2), 1.f * 2.f + 2.f * 4.f + 3.f * 6.f);
    }

    // Case 3: Dot product of opposite vectors
    {
        kpengine::Vector3f v1{1.f, -2.f, 3.f};
        kpengine::Vector3f v2{-1.f, 2.f, -3.f};
        EXPECT_FLOAT_EQ(v1.DotProduct(v2), -1.f - 4.f - 9.f);
    }

    // Case 4: Dot product with itself (should be length squared)
    {
        kpengine::Vector3f v1{2.f, 3.f, 4.f};
        EXPECT_FLOAT_EQ(v1.DotProduct(v1), 2.f * 2.f + 3.f * 3.f + 4.f * 4.f);
    }
}

TEST(Vector3MultipleTest, CrossProduct)
{
    // Case 1: Cross product of perpendicular unit vectors
    {
        kpengine::Vector3f v1{1.f, 0.f, 0.f};
        kpengine::Vector3f v2{0.f, 1.f, 0.f};
        EXPECT_EQ(v1.CrossProduct(v2), kpengine::Vector3f(0.f, 0.f, 1.f));
    }

    // Case 2: Cross product of parallel vectors (should be zero vector)
    {
        kpengine::Vector3f v1{1.f, 2.f, 3.f};
        kpengine::Vector3f v2{2.f, 4.f, 6.f};
        EXPECT_EQ(v1.CrossProduct(v2), kpengine::Vector3f(0.f, 0.f, 0.f));
    }

    // Case 3: Cross product of two arbitrary vectors
    {
        kpengine::Vector3f v1{3.f, -3.f, 1.f};
        kpengine::Vector3f v2{4.f, 9.f, 2.f};
        EXPECT_EQ(v1.CrossProduct(v2), kpengine::Vector3f(-15.f, -2.f, 39.f));
    }

    // Case 4: Cross product follows the right-hand rule
    {
        kpengine::Vector3f v1{0.f, 0.f, 1.f};
        kpengine::Vector3f v2{0.f, 1.f, 0.f};
        EXPECT_EQ(v1.CrossProduct(v2), kpengine::Vector3f(-1.f, 0.f, 0.f));
    }
}
