#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(Matrix3MultiplyTest, MultiplyTwoMatrices)
{
    kpengine::Matrix3f mat1{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f};
    
    // case1: Multiply with a general matrix
    {
        kpengine::Matrix3f mat2{9.f, 8.f, 7.f, 6.f, 5.f, 4.f, 3.f, 2.f, 1.f};
        kpengine::Matrix3f expect{30.f, 24.f, 18.f, 84.f, 69.f, 54.f, 138.f, 114.f, 90.f};
        EXPECT_EQ(mat1 * mat2, expect);
    }
    
    // case2: Multiply with Identity matrix
    {
        kpengine::Matrix3f mat2 = kpengine::Matrix3f::Identity();
        kpengine::Matrix3f expect = mat1;
        EXPECT_EQ(mat1 * mat2, expect);
    }
    
    // case3: Multiply with Zero matrix
    {
        kpengine::Matrix3f mat2{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        kpengine::Matrix3f expect{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        EXPECT_EQ(mat1 * mat2, expect);
    }

    // case4: Multiply with a negative matrix
    {
        kpengine::Matrix3f mat2{-1.f, -2.f, -3.f, -4.f, -5.f, -6.f, -7.f, -8.f, -9.f};
        kpengine::Matrix3f expect{-30.f, -36.f, -42.f, -66.f, -81.f, -96.f, -102.f, -126.f, -150.f};
        EXPECT_EQ(mat1 * mat2, expect);
    }
    
    // case5: Multiply with a scalar matrix (all elements are the same scalar)
    {
        kpengine::Matrix3f mat2{2.f, 2.f, 2.f, 2.f, 2.f, 2.f, 2.f, 2.f, 2.f};
        kpengine::Matrix3f expect{12.f, 12.f, 12.f, 30.f, 30.f, 30.f, 48.f, 48.f, 48.f};
        EXPECT_EQ(mat1 * mat2, expect);
    }
}

TEST(Matrix3MultiplyTest, MultiplyScalarToMatrix)
{
    kpengine::Matrix3f mat{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f};
    
    // case1: Multiply by a positive scalar
    {
        float scalar = 2.f;
        kpengine::Matrix3f expect{2.f, 4.f, 6.f, 8.f, 10.f, 12.f, 14.f, 16.f, 18.f};
        EXPECT_EQ(mat * scalar, expect);
        EXPECT_EQ(scalar * mat, expect);

    }
    
    // case2: Multiply by a negative scalar
    {
        float scalar = -1.f;
        kpengine::Matrix3f expect{-1.f, -2.f, -3.f, -4.f, -5.f, -6.f, -7.f, -8.f, -9.f};
        EXPECT_EQ(mat * scalar, expect);
        EXPECT_EQ(scalar * mat, expect);

    }
    
    // case3: Multiply by zero
    {
        float scalar = 0.f;
        kpengine::Matrix3f expect{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        EXPECT_EQ(mat * scalar, expect);
        EXPECT_EQ(scalar * mat, expect);

    }
}

TEST(Matrix3MultiplyTest, MultiplyMatrixToVector)
{
    kpengine::Matrix3f mat{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f};
    
    // case1: Multiply matrix by vector
    {
        kpengine::Vector3f v{1.f, 2.f, 3.f};
        kpengine::Vector3f expect = {
            1.f * 1.f + 2.f * 2.f + 3.f * 3.f,
            4.f * 1.f + 5.f * 2.f + 6.f * 3.f,
            7.f * 1.f + 8.f * 2.f + 9.f * 3.f
        };
        EXPECT_EQ(mat * v, expect);
    }
    
    // case2: Multiply matrix by zero vector
    {
        kpengine::Vector3f v{0.f, 0.f, 0.f};
        kpengine::Vector3f expect{0.f, 0.f, 0.f};
        EXPECT_EQ(mat * v, expect);
    }
    
    // case3: Multiply matrix by negative vector
    {
        kpengine::Vector3f v{-1.f, -2.f, -3.f};
        kpengine::Vector3f expect = {
            1.f * -1.f + 2.f * -2.f + 3.f * -3.f,
            4.f * -1.f + 5.f * -2.f + 6.f * -3.f,
            7.f * -1.f + 8.f * -2.f + 9.f * -3.f
        };
        EXPECT_EQ(mat * v, expect);
    }
    
    // case4: Multiply identity matrix by vector
    {
        kpengine::Matrix3f identity = kpengine::Matrix3f::Identity();
        kpengine::Vector3f v{4.f, 5.f, 6.f};
        kpengine::Vector3f expect = v;
        EXPECT_EQ(identity * v, expect);
    }
}
