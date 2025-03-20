#include "gtest/gtest.h"
#include "runtime/core/math/math_header.h"

TEST(Matrix4AddTest, AddTwoMatrices)
{
    kpengine::Matrix4f mat1{
        1.f, 2.f, 3.f, 4.f,
        5.f, 6.f, 7.f, 8.f,
        9.f, 10.f, 11.f, 12.f,
        13.f, 14.f, 15.f, 16.f
    };

    // case1: Add identity matrix
    {
        kpengine::Matrix4f mat2 = kpengine::Matrix4f::Identity();
        kpengine::Matrix4f expect{
            2.f, 2.f, 3.f, 4.f,
            5.f, 7.f, 7.f, 8.f,
            9.f, 10.f, 12.f, 12.f,
            13.f, 14.f, 15.f, 17.f
        };
        EXPECT_EQ(mat1 + mat2, expect);
    }

    // case2: Add zero matrix
    {
        kpengine::Matrix4f mat2{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        kpengine::Matrix4f expect = mat1;
        EXPECT_EQ(mat1 + mat2, expect);
    }

    // case3: Add negative matrix
    {
        kpengine::Matrix4f mat2{
            -1.f, -2.f, -3.f, -4.f,
            -5.f, -6.f, -7.f, -8.f,
            -9.f, -10.f, -11.f, -12.f,
            -13.f, -14.f, -15.f, -16.f
        };
        kpengine::Matrix4f expect{
            0.f, 0.f, 0.f, 0.f,
            0.f, 0.f, 0.f, 0.f,
            0.f, 0.f, 0.f, 0.f,
            0.f, 0.f, 0.f, 0.f
        };
        EXPECT_EQ(mat1 + mat2, expect);
    }

    // case4: Add general matrix
    {
        kpengine::Matrix4f mat2{
            16.f, 15.f, 14.f, 13.f,
            12.f, 11.f, 10.f, 9.f,
            8.f, 7.f, 6.f, 5.f,
            4.f, 3.f, 2.f, 1.f
        };
        kpengine::Matrix4f expect{
            17.f, 17.f, 17.f, 17.f,
            17.f, 17.f, 17.f, 17.f,
            17.f, 17.f, 17.f, 17.f,
            17.f, 17.f, 17.f, 17.f
        };
        EXPECT_EQ(mat1 + mat2, expect);
    }
}

TEST(Matrix4AddTest, AddScalarToMatrix)
{
    kpengine::Matrix4f mat{
        1.f, 2.f, 3.f, 4.f,
        5.f, 6.f, 7.f, 8.f,
        9.f, 10.f, 11.f, 12.f,
        13.f, 14.f, 15.f, 16.f
    };

    // case1: Add positive scalar
    {
        float scalar = 2.f;
        kpengine::Matrix4f expect{
            3.f, 4.f, 5.f, 6.f,
            7.f, 8.f, 9.f, 10.f,
            11.f, 12.f, 13.f, 14.f,
            15.f, 16.f, 17.f, 18.f
        };
        EXPECT_EQ(mat + scalar, expect);
        EXPECT_EQ(scalar + mat, expect);

    }

    // case2: Add negative scalar
    {
        float scalar = -1.f;
        kpengine::Matrix4f expect{
            0.f, 1.f, 2.f, 3.f,
            4.f, 5.f, 6.f, 7.f,
            8.f, 9.f, 10.f, 11.f,
            12.f, 13.f, 14.f, 15.f
        };
        EXPECT_EQ(mat + scalar, expect);
        EXPECT_EQ(scalar + mat, expect);
    }

    // case3: Add zero scalar
    {
        float scalar = 0.f;
        kpengine::Matrix4f expect = mat;
        EXPECT_EQ(mat + scalar, expect);
        EXPECT_EQ(scalar + mat, expect);
    }
}
