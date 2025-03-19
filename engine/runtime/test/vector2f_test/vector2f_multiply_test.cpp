#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

//Test multiply operation, dotproduct and cross product


TEST(Vector2MultiplyTest, MultipleTwoVectors)
{
    kpengine::Vector2f v1{1.f, 2.f};

    //case1 
    {
        kpengine::Vector2f v2{1.f, 2.f};
        EXPECT_EQ(v1 * v2, kpengine::Vector2f(1.f, 4.f));
    }
    //case2
    {
        kpengine::Vector2f v2{0.f};
        EXPECT_EQ(v1 * v2, kpengine::Vector2f(0.f));
    }
    //case3
    {
        kpengine::Vector2f v2{1.f, -2.f};
        EXPECT_EQ(v1 * v2, kpengine::Vector2f(1.f, -4.f));
    }
}

TEST(Vector2MultipleTest, MultipleScalarToVector)
{
    kpengine::Vector2f v{1.f, -2.f};

    // Case 1: Multiply by a positive scalar
    {
        float scalar = 2.f;
        EXPECT_EQ(scalar * v, kpengine::Vector2f(2.f, -4.f));
        EXPECT_EQ(v * scalar, kpengine::Vector2f(2.f, -4.f));
    }

    // Case 2: Multiply by zero
    {
        float scalar = 0.f;
        EXPECT_EQ(scalar * v, kpengine::Vector2f(0.f, 0.f));
        EXPECT_EQ(v * scalar, kpengine::Vector2f(0.f, 0.f));
    }

    // Case 3: Multiply by a negative scalar
    {
        float scalar = -1.f;
        EXPECT_EQ(scalar * v, kpengine::Vector2f(-1.f, 2.f));
        EXPECT_EQ(v * scalar, kpengine::Vector2f(-1.f, 2.f));
    }

    // Case 4: Multiply by a fractional scalar
    {
        float scalar = 0.5f;
        EXPECT_EQ(scalar * v, kpengine::Vector2f(0.5f, -1.f));
        EXPECT_EQ(v * scalar, kpengine::Vector2f(0.5f, -1.f));
    }
}

TEST(Vector2MultipleTest, DotProduct)
{
    // Case 1: Dot product of perpendicular vectors (should be 0)
    {
        kpengine::Vector2f v1{1.f, 0.f};
        kpengine::Vector2f v2{0.f, 1.f};
        EXPECT_FLOAT_EQ(v1.DotProduct(v2), 0.f);
    }

    // Case 2: Dot product of parallel vectors
    {
        kpengine::Vector2f v1{1.f, 2.f};
        kpengine::Vector2f v2{2.f, 4.f};
        EXPECT_FLOAT_EQ(v1.DotProduct(v2), 10.f );
    }

    // Case 3: Dot product of opposite vectors
    {
        kpengine::Vector2f v1{1.f, -2.f};
        kpengine::Vector2f v2{-1.f, 2.f};
        EXPECT_FLOAT_EQ(v1.DotProduct(v2), -5.f );
    }

    // Case 4: Dot product with itself (should be length squared)
    {
        kpengine::Vector2f v1{2.f, 3.f};
        EXPECT_FLOAT_EQ(v1.DotProduct(v1), 13.f);
    }
}

