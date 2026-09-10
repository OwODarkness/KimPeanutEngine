#include <gtest/gtest.h>

#include "render/render_submission.h"

namespace kpengine::render
{
    namespace
    {
        graphics::BufferHandle Buffer(const std::uint32_t id)
        {
            return {id, 0u};
        }

        SubmissionDraw MakeDraw()
        {
            SubmissionDraw draw{};
            draw.pipeline = {1u, 0u};
            draw.geometry.vertices = {{0u, Buffer(2u), 0u}};
            draw.geometry.indices = {Buffer(3u), 0u, graphics::IndexElementType::UInt16};
            draw.viewport = {0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
            draw.index_count = 3u;
            draw.uniforms.push_back({0u, 0u, {std::byte{1}}});
            draw.textures.push_back({0u, 1u, {4u, 0u}, {5u, 0u}});
            return draw;
        }
    }

    TEST(RenderSubmissionContractTest, PreservesAuthoredPassAndDrawOrder)
    {
        RenderSubmission submission{};
        submission.buffer_writes.push_back({Buffer(8u), 0u, {std::byte{9}}});
        SubmissionPass first{};
        first.target = {10u, 0u};
        first.draws.push_back(MakeDraw());
        SubmissionPass second{};
        second.target = {11u, 0u};
        second.draws.push_back(MakeDraw());
        second.draws.push_back(MakeDraw());
        submission.passes = {std::move(first), std::move(second)};

        const RenderSubmissionValidation validation = ValidateRenderSubmission(submission);
        ASSERT_TRUE(validation.valid) << validation.diagnostic;
        EXPECT_EQ(validation.draw_count, 3u);
        EXPECT_EQ(validation.upload_bytes, 1u);
        EXPECT_EQ(validation.uniform_bytes, 3u);
        EXPECT_EQ(submission.passes[0].target.id, 10u);
        EXPECT_EQ(submission.passes[1].target.id, 11u);
    }

    TEST(RenderSubmissionContractTest, RejectsInvalidRangesAndDuplicateBindings)
    {
        RenderSubmission submission{};
        SubmissionPass pass{};
        pass.target = {10u, 0u};
        SubmissionDraw draw = MakeDraw();
        draw.uniforms.push_back({0u, 0u, {std::byte{2}}});
        pass.draws.push_back(std::move(draw));
        submission.passes.push_back(std::move(pass));

        const RenderSubmissionValidation validation = ValidateRenderSubmission(submission);
        EXPECT_FALSE(validation.valid);
        EXPECT_NE(validation.diagnostic.find("duplicate"), std::string::npos);
    }

    TEST(RenderSubmissionContractTest, RejectsNonZeroDescriptorSet)
    {
        RenderSubmission submission{};
        SubmissionPass pass{};
        pass.target = {10u, 0u};
        SubmissionDraw draw = MakeDraw();
        draw.uniforms[0].set = 1u;
        pass.draws.push_back(std::move(draw));
        submission.passes.push_back(std::move(pass));

        const RenderSubmissionValidation validation = ValidateRenderSubmission(submission);
        EXPECT_FALSE(validation.valid);
        EXPECT_NE(validation.diagnostic.find("descriptor set zero"), std::string::npos);
    }

    TEST(RenderSubmissionContractTest, AllowsDescriptorFreeDraws)
    {
        RenderSubmission submission{};
        SubmissionPass pass{};
        pass.target = {10u, 0u};
        SubmissionDraw draw = MakeDraw();
        draw.uniforms.clear();
        draw.textures.clear();
        pass.draws.push_back(std::move(draw));
        submission.passes.push_back(std::move(pass));

        const RenderSubmissionValidation validation = ValidateRenderSubmission(submission);
        EXPECT_TRUE(validation.valid) << validation.diagnostic;
    }
}
