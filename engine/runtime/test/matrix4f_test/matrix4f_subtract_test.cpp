#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(Matrix4SubTest, SubtractTwoMatrices)
{
    kpengine::Matrix4f mat1{
        1.f, 2.f, 3.f, 4.f,
        5.f, 6.f, 7.f, 8.f,
        9.f, 10.f, 11.f, 12.f,
        13.f, 14.f, 15.f, 16.f
    };

    // case1: Subtract identity matrix
    {
        kpengine::Matrix4f mat2 = kpengine::Matrix4f::Identity();
        kpengine::Matrix4f expect{
            0.f, 2.f, 3.f, 4.f,
            5.f, 5.f, 7.f, 8.f,
            9.f, 10.f, 10.f, 12.f,
            13.f, 14.f, 15.f, 15.f
        };
        EXPECT_EQ(mat1 - mat2, expect);
    }

    // case2: Subtract zero matrix
    {
        kpengine::Matrix4f mat2{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        kpengine::Matrix4f expect = mat1;
        EXPECT_EQ(mat1 - mat2, expect);
    }

    // case3: Subtract negative matrix
    {
        kpengine::Matrix4f mat2{
            -1.f, -2.f, -3.f, -4.f,
            -5.f, -6.f, -7.f, -8.f,
            -9.f, -10.f, -11.f, -12.f,
            -13.f, -14.f, -15.f, -16.f
        };
        kpengine::Matrix4f expect{
            2.f, 4.f, 6.f, 8.f,
            10.f, 12.f, 14.f, 16.f,
            18.f, 20.f, 22.f, 24.f,
            26.f, 28.f, 30.f, 32.f
        };
        EXPECT_EQ(mat1 - mat2, expect);
    }
}

TEST(Matrix4SubTest, SubtractScalarFromMatrix)
{
    kpengine::Matrix4f mat{
        1.f, 2.f, 3.f, 4.f,
        5.f, 6.f, 7.f, 8.f,
        9.f, 10.f, 11.f, 12.f,
        13.f, 14.f, 15.f, 16.f
    };

    // case1: Subtract positive scalar
    {
        float scalar = 2.f;
        kpengine::Matrix4f expect{
            -1.f, 0.f, 1.f, 2.f,
            3.f, 4.f, 5.f, 6.f,
            7.f, 8.f, 9.f, 10.f,
            11.f, 12.f, 13.f, 14.f
        };
        EXPECT_EQ(mat - scalar, expect);
        EXPECT_EQ(scalar - mat, -expect);

    }

    // case2: Subtract negative scalar
    {
        float scalar = -1.f;
        kpengine::Matrix4f expect{
            2.f, 3.f, 4.f, 5.f,
            6.f, 7.f, 8.f, 9.f,
            10.f, 11.f, 12.f, 13.f,
            14.f, 15.f, 16.f, 17.f
        };
        EXPECT_EQ(mat - scalar, expect);
        EXPECT_EQ(scalar - mat, -expect);

    }

    // case3: Subtract zero scalar
    {
        float scalar = 0.f;
        kpengine::Matrix4f expect = mat;
        EXPECT_EQ(mat - scalar, expect);
        EXPECT_EQ(scalar - mat, -expect);
    }
}
