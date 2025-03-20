#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(Matrix4MulTest, MultiplyTwoMatrices)
{
    kpengine::Matrix4f mat1{
        1.f, 2.f, 3.f, 4.f,
        5.f, 6.f, 7.f, 8.f,
        9.f, 10.f, 11.f, 12.f,
        13.f, 14.f, 15.f, 16.f
    };

    // case1: Multiply by identity matrix
    {
        kpengine::Matrix4f mat2 = kpengine::Matrix4f::Identity();
        kpengine::Matrix4f expect = mat1;
        EXPECT_EQ(mat1 * mat2, expect);
    }

    // case2: Multiply by zero matrix
    {
        kpengine::Matrix4f mat2{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        kpengine::Matrix4f expect{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        EXPECT_EQ(mat1 * mat2, expect);
    }

    // case3: Multiply by general matrix
    {
        kpengine::Matrix4f mat2{
            1.f, 2.f, 3.f, 4.f,
            5.f, 6.f, 7.f, 8.f,
            9.f, 10.f, 11.f, 12.f,
            13.f, 14.f, 15.f, 16.f
        };
        kpengine::Matrix4f expect{
            90.f, 100.f, 110.f, 120.f,
            202.f, 228.f, 254.f, 280.f,
            314.f, 356.f, 398.f, 440.f,
            426.f, 484.f, 542.f, 600.f
        };
        EXPECT_EQ(mat1 * mat2, expect);
    }

    // case4: Multiply by a diagonal matrix
    {
        kpengine::Matrix4f mat2{
            2.f, 0.f, 0.f, 0.f,
            0.f, 3.f, 0.f, 0.f,
            0.f, 0.f, 4.f, 0.f,
            0.f, 0.f, 0.f, 5.f
        };
        kpengine::Matrix4f expect{
            2.f, 6.f, 12.f, 20.f,
            10.f, 18.f, 28.f, 40.f,
            18.f, 30.f, 44.f, 60.f,
            26.f, 42.f, 60.f, 80.f
        };
        EXPECT_EQ(mat1 * mat2, expect);
    }
}

TEST(Matrix4MulTest, MultiplyMatrixByScalar)
{
    kpengine::Matrix4f mat{
        1.f, 2.f, 3.f, 4.f,
        5.f, 6.f, 7.f, 8.f,
        9.f, 10.f, 11.f, 12.f,
        13.f, 14.f, 15.f, 16.f
    };

    // case1: Multiply by positive scalar
    {
        float scalar = 2.f;
        kpengine::Matrix4f expect{
            2.f, 4.f, 6.f, 8.f,
            10.f, 12.f, 14.f, 16.f,
            18.f, 20.f, 22.f, 24.f,
            26.f, 28.f, 30.f, 32.f
        };
        EXPECT_EQ(mat * scalar, expect);
        EXPECT_EQ(scalar * mat, expect);

    }

    // case2: Multiply by negative scalar
    {
        float scalar = -1.f;
        kpengine::Matrix4f expect{
            -1.f, -2.f, -3.f, -4.f,
            -5.f, -6.f, -7.f, -8.f,
            -9.f, -10.f, -11.f, -12.f,
            -13.f, -14.f, -15.f, -16.f
        };
        EXPECT_EQ(mat * scalar, expect);
        EXPECT_EQ(scalar * mat, expect);

    }

    // case3: Multiply by zero scalar
    {
        float scalar = 0.f;
        kpengine::Matrix4f expect{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        EXPECT_EQ(mat * scalar, expect);
        EXPECT_EQ(scalar * mat, expect);

    }
}
