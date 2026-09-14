#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <span>
#include <stdexcept>

#include "math/math_header.h"

namespace
{
    static_assert(kpengine::math::Lerp(0.0f, 10.0f, 0.25f) == 2.5f);
    static_assert(kpengine::math::DegreeToRadian(180.0) == kpengine::math::Math_PI_DOUBLE);

    TEST(MathTest, FixedExtentVectorSpansCopyValues)
    {
        const std::array<float, 2> values2{1.0f, 2.0f};
        const std::array<float, 3> values3{3.0f, 4.0f, 5.0f};
        const std::array<float, 4> values4{6.0f, 7.0f, 8.0f, 9.0f};

        const kpengine::Vector2f vector2{std::span<const float, 2>{values2}};
        const kpengine::Vector3f vector3{std::span<const float, 3>{values3}};
        const kpengine::Vector4f vector4{std::span<const float, 4>{values4}};

        EXPECT_FLOAT_EQ(vector2[1], 2.0f);
        EXPECT_FLOAT_EQ(vector3[2], 5.0f);
        EXPECT_FLOAT_EQ(vector4[3], 9.0f);
    }

    TEST(MathTest, MatrixRowsExposeBoundedViews)
    {
        kpengine::Matrix4f matrix = kpengine::Matrix4f::Identity();
        auto row = matrix[2];
        row[1] = 4.0f;

        EXPECT_EQ(row.size(), 4u);
        EXPECT_FLOAT_EQ(row[1], 4.0f);

        const auto &const_matrix = matrix;
        const auto const_row = const_matrix[2];
        EXPECT_EQ(const_row.size(), 4u);
        EXPECT_FLOAT_EQ(const_row[1], 4.0f);
    }

    TEST(MathTest, OrthographicProjectionPreservesClipMapping)
    {
        const auto projection = kpengine::Matrix4f::MakeOrthProjMatrix(
            -1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f);

        EXPECT_FLOAT_EQ(projection[0][0], 1.0f);
        EXPECT_FLOAT_EQ(projection[1][1], 1.0f);
        EXPECT_FLOAT_EQ(projection[0][3], 0.0f);
        EXPECT_FLOAT_EQ(projection[1][3], 0.0f);
        EXPECT_FLOAT_EQ(projection[3][3], 1.0f);
    }

    TEST(MathTest, MatrixInitializersRejectWrongShape)
    {
        EXPECT_THROW((kpengine::Matrix3f{1.0f, 2.0f}), std::invalid_argument);
        EXPECT_THROW((kpengine::Matrix4f{1.0f, 2.0f}), std::invalid_argument);
    }

    TEST(MathTest, ZeroQuaternionNormalizesToIdentity)
    {
        const kpengine::math::Quaternion<float> zero{0.0f, 0.0f, 0.0f, 0.0f};
        const auto normalized = zero.GetNormalizedQuat();

        EXPECT_FLOAT_EQ(normalized.w_, 1.0f);
        EXPECT_FLOAT_EQ(normalized.x_, 0.0f);
        EXPECT_FLOAT_EQ(normalized.y_, 0.0f);
        EXPECT_FLOAT_EQ(normalized.z_, 0.0f);
    }
}
