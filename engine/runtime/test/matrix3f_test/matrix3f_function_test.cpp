#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(Matrix3FunctionTest, Determinant)
{
    // case1: Determinant of identity matrix
    {
        kpengine::Matrix3f mat = kpengine::Matrix3f::Identity();
        EXPECT_FLOAT_EQ(mat.Determinant(), 1.f);
    }
    
    // case2: Determinant of singular matrix
    {
        kpengine::Matrix3f mat{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f};
        EXPECT_FLOAT_EQ(mat.Determinant(), 0.f);
    }
    
    // case3: Determinant of general matrix
    {
        kpengine::Matrix3f mat{1.f, 7.f, 3.f, 4.f, 10.f, 6.f, 7.f, 8.f, 10.f};
        EXPECT_FLOAT_EQ(mat.Determinant(), -48.f);
    }
}

TEST(Matrix3FunctionTest, Inverse)
{
    // case1: Inverse of identity matrix
    {
        kpengine::Matrix3f mat = kpengine::Matrix3f::Identity();
        EXPECT_EQ(mat.Inverse(), kpengine::Matrix3f::Identity());
    }
    
    // case2: Inverse of a known matrix
    {
        kpengine::Matrix3f mat{
            1, 2, -1,
            3, 4, -2,
            5, -4, 1
        };

        EXPECT_EQ(mat * mat.Inverse(), kpengine::Matrix3f::Identity());
    }
    
    // case3: Inverse of singular matrix (should not exist)
    {
        kpengine::Matrix3f mat{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f};
        EXPECT_THROW(mat.Inverse(), std::runtime_error);
    }
        // case3: Inverse of singular matrix (should not exist)
        {
            kpengine::Matrix3f mat{1.f, 20.f, 36.f, 14.f, 5.f, 2.f, 7.f, 8.f, 9.f};
            EXPECT_EQ(mat * mat.Inverse(), kpengine::Matrix3f::Identity());
        }
}

TEST(Matrix3FunctionTest, Transpose)
{
    // case1: Transpose of identity matrix
    {
        kpengine::Matrix3f mat = kpengine::Matrix3f::Identity();
        EXPECT_EQ(mat.Transpose(), mat);
    }
    
    // case2: Transpose of a general matrix
    {
        kpengine::Matrix3f mat{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f};
        kpengine::Matrix3f expect{1.f, 4.f, 7.f, 2.f, 5.f, 8.f, 3.f, 6.f, 9.f};
        EXPECT_EQ(mat.Transpose(), expect);
    }
    
    // case3: Transpose of already transposed matrix
    {
        kpengine::Matrix3f mat{2.f, 4.f, 6.f, 1.f, 3.f, 5.f, 0.f, 8.f, 7.f};
        EXPECT_EQ(mat.Transpose().Transpose(), mat);
    }
}

TEST(Matrix3FunctionTest, Adjoint)
{
    // case1: Adjoint of identity matrix
    {
        kpengine::Matrix3f mat = kpengine::Matrix3f::Identity();
        EXPECT_EQ(mat.Adjoint(), kpengine::Matrix3f::Identity());
    }
    
    // case2: Adjoint of a general matrix
    {
        kpengine::Matrix3f mat{
            1.f, 2.f, 3.f,
            0.f, 1.f, 4.f,
            5.f, 6.f, 0.f};
        kpengine::Matrix3f expect{
            -24.f, 18.f, 5.f,
            20.f, -15.f, -4.f,
            -5.f, 4.f, 1.f};
        EXPECT_EQ(mat.Adjoint(), expect);
    }
    
    // case3: Adjoint of zero determinant matrix
    {
        kpengine::Matrix3f mat{
            1, 2, -1,
            3, 4, -2,
            5, -4, 1
        };
        kpengine::Matrix3f expect{ 
            -4, 2, 0,
            -13, 6, -1,
            -32, 14, -2
        };
        EXPECT_EQ(mat.Adjoint(), expect);
    }
}
