#include <gtest/gtest.h>

#include <limits>

#include "live2d_model_data.h"

namespace kpengine::live2d
{
    namespace
    {
        Live2DStaticModelData MakeValidData()
        {
            Live2DStaticModelData data{};
            data.topology_revision = 42u;
            data.canvas.size_in_pixels = {100.0f, 200.0f};
            data.canvas.pixels_per_unit = 100.0f;
            data.texture_count = 1u;
            data.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
            data.indices = {0u, 1u, 2u};
            data.drawables.push_back({0u, 3u, 0u, 3u, 0u,
                                      kLive2DNoMaskContext, {}});
            data.maximum_position_bytes = 3u * sizeof(Live2DVector2);
            data.feature_report.drawable_count = 1u;
            return data;
        }
    }

    TEST(Live2DModelDataContractTest, AcceptsValidStaticDataAndFrame)
    {
        Live2DStaticModelData data = MakeValidData();
        std::string diagnostic;
        ASSERT_TRUE(ValidateLive2DStaticModelData(data, diagnostic))
            << diagnostic;

        Live2DFrameSnapshot frame{};
        frame.topology_revision = data.topology_revision;
        frame.frame_sequence = 1u;
        frame.positions = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
        frame.drawables.resize(1u);
        frame.drawables[0].visible = true;
        frame.drawables[0].opacity = 1.0f;
        ASSERT_TRUE(ValidateLive2DFrameSnapshot(data, frame, diagnostic))
            << diagnostic;
    }

    TEST(Live2DModelDataContractTest, RejectsInvalidRangesAndTextureIndices)
    {
        Live2DStaticModelData data = MakeValidData();
        data.drawables[0].vertex_count = 2u;
        std::string diagnostic;
        EXPECT_FALSE(ValidateLive2DStaticModelData(data, diagnostic));
        EXPECT_NE(diagnostic.find("UV or index count"), std::string::npos);

        data = MakeValidData();
        data.drawables[0].texture_index = 1u;
        EXPECT_FALSE(ValidateLive2DStaticModelData(data, diagnostic));
        EXPECT_NE(diagnostic.find("texture index"), std::string::npos);
    }

    TEST(Live2DModelDataContractTest, RejectsNanAndRevisionMismatchAtomically)
    {
        Live2DStaticModelData data = MakeValidData();
        Live2DFrameSnapshot frame{};
        frame.topology_revision = data.topology_revision + 1u;
        frame.frame_sequence = 1u;
        frame.positions = {{0.0f, 0.0f}, {1.0f, 0.0f},
                           {std::numeric_limits<float>::quiet_NaN(), 1.0f}};
        frame.drawables.resize(1u);
        std::string diagnostic;
        EXPECT_FALSE(ValidateLive2DFrameSnapshot(data, frame, diagnostic));
        EXPECT_NE(diagnostic.find("topology revision"), std::string::npos);

        frame.topology_revision = data.topology_revision;
        EXPECT_FALSE(ValidateLive2DFrameSnapshot(data, frame, diagnostic));
        EXPECT_NE(diagnostic.find("non-finite"), std::string::npos);
    }

    TEST(Live2DModelDataContractTest, RejectsUnsupportedFeatureReport)
    {
        Live2DStaticModelData data = MakeValidData();
        data.feature_report.offscreen_object_count = 1u;
        std::string diagnostic;
        EXPECT_FALSE(ValidateLive2DStaticModelData(data, diagnostic));
        EXPECT_NE(diagnostic.find("unsupported"), std::string::npos);
    }
}
