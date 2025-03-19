#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(Matrix3AddTest, AddTwoMatrices)
{
    kpengine::Matrix3f mat1{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f};
    
    // case1: Add identity matrix
    {
        kpengine::Matrix3f mat2 = kpengine::Matrix3f::Identity();
        kpengine::Matrix3f expect{2.f, 2.f, 3.f, 4.f, 6.f, 6.f, 7.f, 8.f, 10.f};
        EXPECT_EQ(mat1 + mat2, expect);
    }
    
    // case2: Add zero matrix
    {
        kpengine::Matrix3f mat2{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        kpengine::Matrix3f expect = mat1;
        EXPECT_EQ(mat1 + mat2, expect);
    }
    
    // case3: Add negative matrix
    {
        kpengine::Matrix3f mat2{-1.f, -2.f, -3.f, -4.f, -5.f, -6.f, -7.f, -8.f, -9.f};
        kpengine::Matrix3f expect{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        EXPECT_EQ(mat1 + mat2, expect);
    }
    
    // case4: Add general matrix
    {
        kpengine::Matrix3f mat2{9.f, 8.f, 7.f, 6.f, 5.f, 4.f, 3.f, 2.f, 1.f};
        kpengine::Matrix3f expect{10.f, 10.f, 10.f, 10.f, 10.f, 10.f, 10.f, 10.f, 10.f};
        EXPECT_EQ(mat1 + mat2, expect);
    }
}

TEST(Matrix3AddTest, AddScalarToMatrix)
{
    kpengine::Matrix3f mat{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f};
    
    // case1: Add positive scalar
    {
        float scalar = 2.f;
        kpengine::Matrix3f expect{3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f};
        EXPECT_EQ(mat + scalar, expect);
        EXPECT_EQ(scalar + mat, expect);
    }
    
    // case2: Add negative scalar
    {
        float scalar = -1.f;
        kpengine::Matrix3f expect{0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
        EXPECT_EQ(mat + scalar, expect);
        EXPECT_EQ(scalar + mat, expect);
    }
    
    // case3: Add zero scalar
    {
        float scalar = 0.f;
        kpengine::Matrix3f expect = mat;
        EXPECT_EQ(mat + scalar, expect);
        EXPECT_EQ(scalar + mat, expect);
    }
}
