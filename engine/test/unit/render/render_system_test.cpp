#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "asset/asset_manager.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "data/texture.h"
#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
#include "log/log_system.h"
#include "resource/resource_pipeline.h"
#include "render/deferred_renderer.h"
#include "render/render_resource_resolver.h"
#include "render/render_system.h"
#include "render/render_submission_executor.h"
#include "render/prepared_render_asset_catalog.h"
#include "render/passes/capture_view_pass.h"
#include "render/passes/scene_draw_recorder.h"
#include "render/path_trace_history_signature.h"
#include "render/path_trace_history_progress.h"
#include "render/path_trace_adaptive_spp.h"
#include "render/path_trace_settings.h"
#include "render/ray_tracing_scene_signature.h"
#include "support/fake_render_backend.h"

namespace
{
    using namespace kpengine;
    using namespace kpengine::test;

    TEST(GraphicsGpuProfileRegistration, AcceptsUniqueStableIdsAndPreservesOrder)
    {
        const auto probe = std::make_shared<BackendProbe>();
        FakeBackend backend{probe};
        const std::array<uint32_t, 3> ids{0x4b504501u, 0x4b504502u, 0x4b504503u};

        ASSERT_TRUE(backend.ConfigureGpuProfilePasses(ids));
        const auto configured = backend.GetConfiguredGpuProfilePasses();
        ASSERT_EQ(configured.size(), ids.size());
        EXPECT_TRUE(std::equal(configured.begin(), configured.end(), ids.begin()));
    }

    TEST(GraphicsGpuProfileRegistration, RejectsDuplicateIdsWithoutChangingRegistration)
    {
        const auto probe = std::make_shared<BackendProbe>();
        FakeBackend backend{probe};
        const std::array<uint32_t, 2> valid_ids{0x4b504511u, 0x4b504512u};
        const std::array<uint32_t, 2> duplicate_ids{0x4b504521u, 0x4b504521u};

        ASSERT_TRUE(backend.ConfigureGpuProfilePasses(valid_ids));
        EXPECT_FALSE(backend.ConfigureGpuProfilePasses(duplicate_ids));
        const auto configured = backend.GetConfiguredGpuProfilePasses();
        ASSERT_EQ(configured.size(), valid_ids.size());
        EXPECT_TRUE(std::equal(configured.begin(), configured.end(), valid_ids.begin()));
    }

    TEST(GraphicsGpuProfileRegistration, RejectsOverCapacityAndPostInitializationChanges)
    {
        const auto probe = std::make_shared<BackendProbe>();
        FakeBackend backend{probe};
        std::array<uint32_t, graphics::kMaxGpuProfilePasses + 1> too_many_ids{};
        for (size_t index = 0; index < too_many_ids.size(); ++index)
        {
            too_many_ids[index] = static_cast<uint32_t>(0x4b505000u + index);
        }
        EXPECT_FALSE(backend.ConfigureGpuProfilePasses(too_many_ids));
        EXPECT_TRUE(backend.GetConfiguredGpuProfilePasses().empty());

        const std::array<uint32_t, 1> configured_id{0x4b504531u};
        ASSERT_TRUE(backend.ConfigureGpuProfilePasses(configured_id));
        backend.FinalizeProfileConfigurationForTest();
        const std::array<uint32_t, 1> late_id{0x4b504532u};
        EXPECT_FALSE(backend.ConfigureGpuProfilePasses(late_id));
        EXPECT_EQ(backend.GetConfiguredGpuProfilePasses().front(), configured_id.front());
    }

    TEST(RenderGpuProfileIds, CanonicalIdsAreStableAndResolveToTheirPass)
    {
        for (size_t index = 0; index < static_cast<size_t>(render::RenderProfilePass::Count); ++index)
        {
            const auto pass = static_cast<render::RenderProfilePass>(index);
            const uint32_t id = render::GetRenderGpuProfilePassId(pass);
            EXPECT_NE(id, 0u);
            EXPECT_EQ(render::GetRenderProfilePassIndex(id), index);
        }
        EXPECT_EQ(render::GetRenderProfilePassIndex(0u),
                  static_cast<size_t>(render::RenderProfilePass::Count));
    }

    TEST(RayTracingSceneSignatureTest, TracksGeometryResourcesAndTopology)
    {
        std::array<graphics::RayTracingGeometryDesc, 1> geometries{};
        geometries[0].vertex_buffer = {3, 1};
        geometries[0].vertex_offset = 16;
        geometries[0].vertex_stride = 32;
        geometries[0].vertex_count = 12;
        geometries[0].index_buffer = {4, 1};
        geometries[0].index_offset = 8;
        geometries[0].index_count = 36;
        const uint64_t baseline = render::detail::RayTracingGeometrySignature(geometries);

        auto changed = geometries;
        changed[0].vertex_buffer.generation++;
        EXPECT_NE(render::detail::RayTracingGeometrySignature(changed), baseline);
        changed = geometries;
        changed[0].index_offset++;
        EXPECT_NE(render::detail::RayTracingGeometrySignature(changed), baseline);
        changed = geometries;
        changed[0].index_count += 3;
        EXPECT_NE(render::detail::RayTracingGeometrySignature(changed), baseline);
        EXPECT_NE(render::detail::RayTracingGeometrySignature(
                      std::span<const graphics::RayTracingGeometryDesc>{}),
                  baseline);
    }

    TEST(RayTracingSectionSelectionTest, MixedMeshKeepsOriginalMaterialSectionOrder)
    {
        using render::MaterialDrawClass;
        const std::array<std::optional<MaterialDrawClass>, 4> classes{
            MaterialDrawClass::Opaque, MaterialDrawClass::AlphaBlend,
            std::nullopt, MaterialDrawClass::Opaque};
        const auto key = render::detail::MakeOpaqueRayTracingSectionKey({3, 1}, classes);
        EXPECT_EQ(key.section_indices, (std::vector<uint32_t>{0, 3}));
        const std::array<uint32_t, 4> material_slots{8, 9, 10, 11};
        EXPECT_EQ(material_slots[key.section_indices[1]], 11U);
    }

    TEST(RayTracingSectionSelectionTest, SameMeshWithDifferentOverridesDoesNotShareBlas)
    {
        using render::MaterialDrawClass;
        const std::array<std::optional<MaterialDrawClass>, 2> opaque{
            MaterialDrawClass::Opaque, MaterialDrawClass::Opaque};
        const std::array<std::optional<MaterialDrawClass>, 2> mixed{
            MaterialDrawClass::AlphaBlend, MaterialDrawClass::Opaque};
        const auto full = render::detail::MakeOpaqueRayTracingSectionKey({3, 1}, opaque);
        const auto subset = render::detail::MakeOpaqueRayTracingSectionKey({3, 1}, mixed);
        std::unordered_map<render::detail::RayTracingSectionKey, int,
                           render::detail::RayTracingSectionKeyHash> cache;
        cache[full] = 1;
        cache[subset] = 2;
        EXPECT_EQ(cache.size(), 2U);
        EXPECT_EQ(cache.at(render::detail::MakeOpaqueRayTracingSectionKey({3, 1}, opaque)), 1);
        EXPECT_NE(full, render::detail::MakeOpaqueRayTracingSectionKey({3, 2}, opaque));
    }

    TEST(RayTracingSectionSelectionTest, BlendedOnlyMeshDoesNotEnterOpaqueScene)
    {
        const std::array<std::optional<render::MaterialDrawClass>, 2> classes{
            render::MaterialDrawClass::AlphaBlend, std::nullopt};
        EXPECT_TRUE(render::detail::MakeOpaqueRayTracingSectionKey({3, 1}, classes)
                        .section_indices.empty());
    }

    TEST(PathTraceHistorySignatureTest, ChangesForEveryAccumulationInput)
    {
        using render::detail::ComputePathTraceHistorySignature;
        using render::detail::PathTraceHistorySignatureInput;

        PathTraceHistorySignatureInput input{};
        input.width = 1094;
        input.height = 619;
        input.scene_signature = 42;
        input.geometry_count = 8;
        input.pipeline_id = 3;
        input.pipeline_generation = 2;
        input.view_projection[0] = 1.0f;
        input.view_projection[5] = 1.0f;
        input.view_projection[10] = 1.0f;
        input.view_projection[15] = 1.0f;
        input.camera_position = {0.0f, 1.0f, 5.0f};
        const uint64_t baseline = ComputePathTraceHistorySignature(input);
        EXPECT_EQ(ComputePathTraceHistorySignature(input), baseline);

        auto changed = input;
        ++changed.width;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        ++changed.height;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        ++changed.scene_signature;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        ++changed.geometry_count;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        ++changed.material_signature;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        ++changed.pipeline_id;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        ++changed.pipeline_generation;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        ++changed.shader_signature;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        ++changed.probe_mode;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        changed.light_parameters[4] += 0.25f;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        changed.ray_parameters[2] += 0.001f;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        changed.batch_samples_per_dispatch = 4;
        EXPECT_EQ(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        ++changed.integrator_parameters[0];
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        ++changed.rng_seed;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        ++changed.rng_policy_version;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        changed.view_projection[12] += 0.25f;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
        changed = input;
        changed.camera_position[0] += 0.25f;
        EXPECT_NE(ComputePathTraceHistorySignature(changed), baseline);
    }

    TEST(PathTraceHistorySignatureTest, ReconstructionTransitionPreservesHistoryMode)
    {
        render::PathTraceSettings settings{};
        settings.reconstruction = render::PathTraceReconstruction::GuidedPreview;
        const auto moving_mode = render::PackPathTraceHistoryMode(settings);
        settings.reconstruction = render::PathTraceReconstruction::Raw;
        EXPECT_EQ(render::PackPathTraceHistoryMode(settings), moving_mode);
        settings.reconstruction = render::PathTraceReconstruction::VarianceDenoise;
        EXPECT_EQ(render::PackPathTraceHistoryMode(settings), moving_mode);
        settings.direct_light_sampling = render::PathTraceDirectLightSampling::UniformOneLight;
        EXPECT_NE(render::PackPathTraceHistoryMode(settings), moving_mode);
    }

    TEST(PathTraceHistorySignatureTest, ReportsMaterialResidencyAndCameraInvalidation)
    {
        using render::detail::DescribePathTraceHistoryChange;
        render::detail::PathTraceHistorySignatureInput previous{};
        auto current = previous;
        ++current.material_signature;
        EXPECT_STREQ(DescribePathTraceHistoryChange(previous, current), "materials_changed");
        current = previous;
        current.camera_position[0] = 0.5f;
        EXPECT_STREQ(DescribePathTraceHistoryChange(previous, current), "camera_changed");
        current = previous;
        ++current.probe_mode;
        EXPECT_STREQ(DescribePathTraceHistoryChange(previous, current), "integrator_changed");
    }

    TEST(PathTraceSettingsTest, SingleLightPreviewPreservesVisibilityAndBouncePolicy)
    {
        using render::ApplyLegacyPathTraceProbeMode;
        using render::PackPathTraceShaderMode;
        using render::PathTraceDirectLightSampling;
        using render::PathTraceProbeMode;
        using render::PathTraceReconstruction;
        using render::PathTraceSettings;
        using render::PathTraceVisibilityMethod;

        PathTraceSettings settings{};
        settings.visibility_method = PathTraceVisibilityMethod::RayQuery;
        settings.maximum_continuation_bounces = 8;
        const PathTraceSettings preview = ApplyLegacyPathTraceProbeMode(
            settings, PathTraceProbeMode::SingleLightPreview);

        EXPECT_TRUE(render::IsValidPathTraceSettings(preview));
        EXPECT_EQ(preview.visibility_method, PathTraceVisibilityMethod::RayQuery);
        EXPECT_EQ(preview.samples_per_dispatch, 1u);
        EXPECT_EQ(preview.maximum_continuation_bounces, 8u);
        EXPECT_EQ(preview.reconstruction, PathTraceReconstruction::GuidedPreview);
        EXPECT_EQ(preview.direct_light_sampling,
                  PathTraceDirectLightSampling::UniformOneLight);
        EXPECT_NE(PackPathTraceShaderMode(preview) & (1u << 11u), 0u);
    }

    TEST(PathTraceSettingsTest, ParsesIndependentEstimatorAndReconstructionSettings)
    {
        using render::ParsePathTraceDirectLightSampling;
        using render::ParsePathTraceOutputProbe;
        using render::ParsePathTraceReconstruction;
        using render::ParsePathTraceSamplingPolicy;
        using render::ParsePathTraceVisibilityMethod;
        using render::PathTraceDirectLightSampling;
        using render::PathTraceOutputProbe;
        using render::PathTraceReconstruction;
        using render::PathTraceVisibilityMethod;

        const auto visibility = ParsePathTraceVisibilityMethod("ray_query");
        const auto reconstruction = ParsePathTraceReconstruction("raw");
        const auto sampling = ParsePathTraceDirectLightSampling("all_lights");
        const auto policy = ParsePathTraceSamplingPolicy("adaptive_camera_motion");
        const auto output = ParsePathTraceOutputProbe("beauty");
        ASSERT_TRUE(visibility.has_value());
        ASSERT_TRUE(reconstruction.has_value());
        ASSERT_TRUE(sampling.has_value());
        ASSERT_TRUE(policy.has_value());
        ASSERT_TRUE(output.has_value());
        EXPECT_EQ(*visibility, PathTraceVisibilityMethod::RayQuery);
        EXPECT_EQ(*reconstruction, PathTraceReconstruction::Raw);
        EXPECT_EQ(*sampling, PathTraceDirectLightSampling::AllLights);
        EXPECT_EQ(*policy, render::PathTraceSamplingPolicy::AdaptiveCameraMotion);
        EXPECT_EQ(*output, PathTraceOutputProbe::Beauty);
        EXPECT_FALSE(ParsePathTraceReconstruction("guided").has_value());

        render::PathTraceSettings settings{};
        settings.visibility_method = *visibility;
        settings.samples_per_dispatch = 1;
        settings.maximum_continuation_bounces = 8;
        settings.reconstruction = *reconstruction;
        settings.direct_light_sampling = *sampling;
        settings.sampling_policy = *policy;
        settings.output_probe = *output;
        EXPECT_TRUE(render::IsValidPathTraceSettings(settings));
        EXPECT_EQ(render::PackPathTraceShaderMode(settings) & (1u << 10u), 0u);
        EXPECT_EQ(render::PackPathTraceShaderMode(settings) & (1u << 11u), 0u);
        EXPECT_TRUE(render::IsValidPathTraceSettings(settings));
    }

    TEST(PathTraceSettingsTest, AdaptiveSingleSppUsesFrameSequenceIndependentOfDenoiser)
    {
        using render::PackPathTraceShaderMode;
        using render::PathTraceSamplingPolicy;
        using render::PathTraceSettings;

        PathTraceSettings settings{};
        settings.samples_per_dispatch = 1;
        settings.reconstruction = render::PathTraceReconstruction::VarianceDenoise;
        EXPECT_NE(PackPathTraceShaderMode(settings) & (1u << 12u), 0u);

        settings.samples_per_dispatch = 2;
        EXPECT_NE(PackPathTraceShaderMode(settings) & (1u << 12u), 0u);

        settings.sampling_policy = PathTraceSamplingPolicy::Fixed;
        settings.samples_per_dispatch = 1;
        EXPECT_EQ(PackPathTraceShaderMode(settings) & (1u << 12u), 0u);
    }

    TEST(PathTraceAdaptiveSppTest, RequiresMotionThresholdAndStillFramesBeforeSettling)
    {
        using render::detail::EvaluatePathTraceAdaptiveSpp;
        using render::detail::PathTraceAdaptiveSppState;
        using render::detail::PathTraceCameraMotionSample;

        PathTraceAdaptiveSppState state{};
        PathTraceCameraMotionSample camera{};
        auto decision = EvaluatePathTraceAdaptiveSpp(state, camera, 0.02f, 0.2f, 3u);
        EXPECT_TRUE(decision.camera_moving);
        state = decision.next_state;

        decision = EvaluatePathTraceAdaptiveSpp(state, camera, 0.02f, 0.2f, 3u);
        EXPECT_TRUE(decision.camera_moving);
        state = decision.next_state;
        decision = EvaluatePathTraceAdaptiveSpp(state, camera, 0.02f, 0.2f, 3u);
        EXPECT_TRUE(decision.camera_moving);
        state = decision.next_state;
        decision = EvaluatePathTraceAdaptiveSpp(state, camera, 0.02f, 0.2f, 3u);
        EXPECT_FALSE(decision.camera_moving);
        state = decision.next_state;

        camera.position[0] = 0.03f;
        decision = EvaluatePathTraceAdaptiveSpp(state, camera, 0.02f, 0.2f, 3u);
        EXPECT_TRUE(decision.camera_moving);
        EXPECT_EQ(decision.stable_frames, 0u);
    }

    TEST(PathTraceAdaptiveSppTest, AccumulatesSubthresholdTravelAndWrapsYaw)
    {
        using render::detail::EvaluatePathTraceAdaptiveSpp;
        using render::detail::PathTraceAdaptiveSppState;
        using render::detail::PathTraceCameraMotionSample;

        PathTraceAdaptiveSppState state{};
        PathTraceCameraMotionSample camera{};
        camera.rotation_degrees[1] = 179.9f;
        for (uint32_t frame = 0; frame < 5u; ++frame)
        {
            state = EvaluatePathTraceAdaptiveSpp(
                state, camera, 0.02f, 0.3f, 4u).next_state;
        }
        EXPECT_FALSE(state.camera_moving);
        camera.rotation_degrees[1] = -179.9f;
        auto wrapped = EvaluatePathTraceAdaptiveSpp(state, camera, 0.02f, 0.3f, 4u);
        EXPECT_FALSE(wrapped.camera_moving);
        state = wrapped.next_state;

        camera.rotation_degrees[1] = -179.9f;
        camera.position[0] = 0.01f;
        auto decision = EvaluatePathTraceAdaptiveSpp(state, camera, 0.02f, 0.3f, 4u);
        EXPECT_FALSE(decision.camera_moving);
        state = decision.next_state;
        camera.position[0] = 0.02f;
        decision = EvaluatePathTraceAdaptiveSpp(state, camera, 0.02f, 0.3f, 4u);
        EXPECT_TRUE(decision.camera_moving);
    }

    TEST(PathTraceAdaptiveSppTest, StepsFromFourToTwoToOneSppWithoutResettingHistory)
    {
        using render::detail::PathTraceAdaptiveSppPhase;
        using render::detail::SelectPathTraceAdaptiveSpp;

        const render::PathTraceSettings defaults{};
        EXPECT_EQ(defaults.adaptive_moving_reconstruction,
                  render::PathTraceReconstruction::GuidedPreview);
        EXPECT_EQ(defaults.sampling_policy,
                  render::PathTraceSamplingPolicy::AdaptiveCameraMotion);
        EXPECT_TRUE(render::IsValidPathTraceSettings(defaults));
        EXPECT_EQ(defaults.quality_2spp_sample_threshold, 100u);
        EXPECT_EQ(defaults.quality_1spp_sample_threshold, 200u);
        EXPECT_EQ(defaults.settled_samples_per_dispatch, 4u);
        EXPECT_EQ(defaults.quality_2spp_samples_per_dispatch, 2u);
        EXPECT_EQ(defaults.quality_maintenance_samples_per_dispatch, 1u);

        auto selected = SelectPathTraceAdaptiveSpp(
            true, 800u, 100u, 200u, 1u, 4u, 2u, 1u);
        EXPECT_EQ(selected.phase, PathTraceAdaptiveSppPhase::Moving);
        EXPECT_EQ(selected.samples_per_dispatch, 1u);

        selected = SelectPathTraceAdaptiveSpp(
            false, 96u, 100u, 200u, 1u, 4u, 2u, 1u);
        EXPECT_EQ(selected.phase, PathTraceAdaptiveSppPhase::ConvergingHighSpp);
        EXPECT_EQ(selected.samples_per_dispatch, 4u);
        const auto at_threshold = render::detail::CommitPathTraceHistoryProgress(
            {96u, 0u}, true, false, false, true, selected.samples_per_dispatch);
        ASSERT_EQ(at_threshold.sample_count, 100u);

        selected = SelectPathTraceAdaptiveSpp(
            false, at_threshold.sample_count, 100u, 200u, 1u, 4u, 2u, 1u);
        EXPECT_EQ(selected.phase, PathTraceAdaptiveSppPhase::ConvergingMediumSpp);
        EXPECT_EQ(selected.samples_per_dispatch, 2u);
        const auto near_quality = render::detail::CommitPathTraceHistoryProgress(
            {198u, at_threshold.write_index}, true, false, false, true,
            selected.samples_per_dispatch);
        ASSERT_EQ(near_quality.sample_count, 200u);

        selected = SelectPathTraceAdaptiveSpp(
            false, near_quality.sample_count, 100u, 200u, 1u, 4u, 2u, 1u);
        EXPECT_EQ(selected.phase, PathTraceAdaptiveSppPhase::QualityMaintenance);
        EXPECT_EQ(selected.samples_per_dispatch, 1u);
        const auto maintained = render::detail::CommitPathTraceHistoryProgress(
            near_quality, true, false, false, true, selected.samples_per_dispatch);
        EXPECT_EQ(maintained.sample_count, 201u);
    }

    TEST(PathTraceAdaptiveSppTest, UsesConfiguredDenoiseWhileMovingAndRawWhenStable)
    {
        using render::PathTraceReconstruction;
        using render::detail::SelectAdaptivePathTraceReconstruction;

        EXPECT_EQ(SelectAdaptivePathTraceReconstruction(
                      true, PathTraceReconstruction::VarianceDenoise),
                  PathTraceReconstruction::VarianceDenoise);
        EXPECT_EQ(SelectAdaptivePathTraceReconstruction(
                      false, PathTraceReconstruction::VarianceDenoise),
                  PathTraceReconstruction::Raw);
        EXPECT_EQ(SelectAdaptivePathTraceReconstruction(
                      true, PathTraceReconstruction::GuidedPreview),
                  PathTraceReconstruction::GuidedPreview);
    }

    TEST(PathTraceHistoryProgressTest, FailedFrameDoesNotAdvanceOrSwapHistory)
    {
        using render::detail::CommitPathTraceHistoryProgress;
        using render::detail::PathTraceHistoryProgress;

        const PathTraceHistoryProgress current{128, 1};
        EXPECT_EQ(CommitPathTraceHistoryProgress(current, true, false, true, true, 4),
                  current);
        EXPECT_EQ(CommitPathTraceHistoryProgress(current, true, true, false, true, 4),
                  current);
        EXPECT_EQ(CommitPathTraceHistoryProgress(current, false, false, false, true, 4),
                  current);
        EXPECT_EQ(CommitPathTraceHistoryProgress(current, true, false, false, false, 4),
                  current);

        EXPECT_EQ(CommitPathTraceHistoryProgress(current, true, false, false, true, 4),
                  (PathTraceHistoryProgress{132, 0}));
    }

    TEST(PathTraceHistoryProgressTest, RandomFrameIndexAdvancesOnlyOnCommittedPathTraceFrames)
    {
        using render::detail::CommitPathTraceRandomFrameIndex;

        constexpr uint32_t current = 17;
        EXPECT_EQ(CommitPathTraceRandomFrameIndex(current, false, false, false, true, 1), current);
        EXPECT_EQ(CommitPathTraceRandomFrameIndex(current, true, true, false, true, 1), current);
        EXPECT_EQ(CommitPathTraceRandomFrameIndex(current, true, false, true, true, 1), current);
        EXPECT_EQ(CommitPathTraceRandomFrameIndex(current, true, false, false, false, 1), current);
        EXPECT_EQ(CommitPathTraceRandomFrameIndex(current, true, false, false, true, 1), current + 1);
        EXPECT_EQ(CommitPathTraceRandomFrameIndex(current, true, false, false, true, 4), current + 4);
    }

    TEST(RenderSubmissionExecutorTest, PreparesAndRecordsGenericWorkInOrder)
    {
        const auto probe = std::make_shared<BackendProbe>();
        FakeBackend backend(probe);
        backend.Initialize({});

        render::FrameContext frame;
        frame.Initialize(backend, 256u);
        frame.Begin(0u, {1u, 0.0f, 1.0f / 60.0f}, {64u, 64u});

        graphics::BufferDesc buffer_desc{};
        buffer_desc.role = graphics::BufferRole::Vertex;
        buffer_desc.update_mode = graphics::BufferUpdateMode::PerFrame;
        buffer_desc.capacity_bytes = 64u;
        const graphics::BufferHandle position_buffer =
            backend.CreateBuffer(buffer_desc, {});
        const graphics::RenderTargetDesc target_desc{
            64u, 64u, 1u, {graphics::RenderTargetColorAttachment{}}, std::nullopt};
        const graphics::RenderTargetHandle target = backend.CreateRenderTarget(target_desc);

        render::RenderSubmission submission{};
        submission.buffer_writes.push_back({position_buffer, 0u, {std::byte{7}}});
        render::SubmissionPass pass{};
        pass.target = target;
        render::SubmissionDraw draw{};
        draw.pipeline = {20u, 0u};
        draw.geometry.vertices = {{0u, position_buffer, 0u}};
        draw.geometry.indices = {{21u, 0u}, 0u, graphics::IndexElementType::UInt16};
        draw.viewport = {0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
        draw.index_count = 3u;
        draw.uniforms.push_back({0u, 0u, {std::byte{1}, std::byte{2}}});
        draw.textures.push_back({0u, 1u, {22u, 0u}, {23u, 0u}});
        pass.draws.push_back(std::move(draw));
        submission.passes.push_back(std::move(pass));

        const render::RenderSubmissionExecutionResult result =
            render::RenderSubmissionExecutor::Execute(
                submission, frame, *backend.GetCommandRecorder());
        ASSERT_TRUE(result.succeeded) << result.diagnostic;
        EXPECT_EQ(result.pass_count, 1u);
        EXPECT_EQ(result.draw_count, 1u);
        EXPECT_EQ(result.upload_bytes, 1u);
        EXPECT_EQ(result.uniform_bytes, 2u);
        EXPECT_EQ(result.binding_count, 1u);
        EXPECT_EQ(probe->events.back(), "end_target");

        frame.End();
        frame.Cleanup();
        backend.DestroyRenderTarget(target);
        backend.Cleanup();
    }

    std::shared_ptr<const render::PreparedRenderAssetCatalog> BuildPreparedCatalog(
        const std::vector<asset::AssetID> &extra_textures = {})
    {
        render::PreparedRenderAssetCatalogBuild build;
        build.graphics_api = GraphicsAPIType::GRAPHICS_API_OPENGL;
        const asset::AssetID vertex_id{1, 1, asset::AssetType::KPAT_Shader};
        const asset::AssetID fragment_id{2, 1, asset::AssetType::KPAT_Shader};
        const asset::AssetID program_id{3, 1, asset::AssetType::KPAT_ShaderProgram};

        auto make_shader = [](ShaderStage stage)
        {
            auto shader = std::make_shared<asset::ShaderResource>();
            shader->status = asset::ShaderStatus::Ready;
            shader->data = std::make_shared<data::ShaderData>();
            shader->data->stage = stage;
            shader->data->api = GraphicsAPIType::GRAPHICS_API_OPENGL;
            shader->data->source = "void main() {}";
            shader->desc.stage = stage;
            shader->format = ShaderFormat::SHADER_FORMAT_GLSL;
            return shader;
        };
        build.records.push_back({vertex_id, make_shader(ShaderStage::SHADER_STAGE_VERTEX), {}});
        build.records.push_back({fragment_id, make_shader(ShaderStage::SHADER_STAGE_FRAGMENT), {}});
        auto program = std::make_shared<asset::ShaderProgramResource>();
        program->BindData(ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL,
                          vertex_id);
        program->BindData(ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL,
                          fragment_id);
        build.records.push_back({program_id, program, {vertex_id, fragment_id}});

        auto make_texture = [](uint32_t value)
        {
            auto texture = std::make_shared<asset::TextureResource>();
            texture->data->width = 1;
            texture->data->height = 1;
            texture->data->pixels.resize(4, static_cast<uint8_t>(value));
            return texture;
        };
        const asset::AssetID white_id{4, 1, asset::AssetType::KPAT_Texture};
        const asset::AssetID normal_id{5, 1, asset::AssetType::KPAT_Texture};
        build.records.push_back({white_id, make_texture(255), {}});
        build.records.push_back({normal_id, make_texture(128), {}});
        for (const asset::AssetID extra : extra_textures)
        {
            const auto texture = asset::AssetManager::GetInstance().GetResource<asset::TextureResource>(extra);
            if (texture)
            {
                build.records.push_back({extra, texture, {}});
                if (texture->data && texture->data->format == TextureFormat::TEXTURE_FORMAT_RGBA16F &&
                    texture->data->pixels.size() >= 64)
                {
                    resource::EnvironmentIblData ibl{};
                    ibl.irradiance.width = 1;
                    ibl.irradiance.height = 1;
                    ibl.irradiance.format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
                    ibl.irradiance.pixels.resize(8, 0);
                    ibl.prefiltered_radiance.width = 1;
                    ibl.prefiltered_radiance.height = 1;
                    ibl.prefiltered_radiance.format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
                    ibl.prefiltered_radiance.pixels.resize(8, 0);
                    ibl.brdf_lut.width = 1;
                    ibl.brdf_lut.height = 1;
                    ibl.brdf_lut.format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
                    ibl.brdf_lut.pixels.resize(8, 0);
                    ibl.prefilter_level_count = 1;
                    build.environment_ibl.push_back({extra, std::move(ibl)});
                }
            }
        }
        for (const auto &requirement : render::GetBuiltInRenderAssetRequirements())
        {
            build.built_ins[static_cast<size_t>(requirement.role)] =
                requirement.expected_type == asset::AssetType::KPAT_Texture
                    ? (requirement.role == render::BuiltInRenderAsset::DefaultWhiteTexture
                           ? white_id
                           : normal_id)
                    : program_id;
        }
        std::string diagnostic;
        auto catalog = render::PreparedRenderAssetCatalog::Create(std::move(build), diagnostic);
        return catalog ? std::make_shared<const render::PreparedRenderAssetCatalog>(std::move(*catalog))
                       : nullptr;
    }

    TEST(RenderResourceResolverTest, GBufferPipelineMatchesTemporalAttachmentAndUniformStages)
    {
        auto probe = std::make_shared<BackendProbe>();
        FakeBackend backend(probe);
        const auto prepared_assets = BuildPreparedCatalog();
        ASSERT_NE(prepared_assets, nullptr);
        const asset::AssetID program_id = prepared_assets->GetBuiltIn(
            render::BuiltInRenderAsset::GBufferDebugProgram);
        const auto program = prepared_assets->Get<asset::ShaderProgramResource>(program_id);
        ASSERT_NE(program, nullptr);

        render::RenderResourceResolver resolver(backend, *prepared_assets);
        const auto pipeline = resolver.GetOrCreateDefaultPipeline(
            program_id, *program, nullptr, false, render::MaterialPass::GBuffer);
        ASSERT_TRUE(pipeline.IsValid());
        ASSERT_EQ(probe->pipelines.size(), 1u);

        const auto &description = probe->pipelines.front();
        ASSERT_EQ(description.color_attachment_formats.size(), 5u);
        EXPECT_EQ(description.color_attachment_formats.back(),
                  TextureFormat::TEXTURE_FORMAT_RGBA16F);
        ASSERT_EQ(description.descriptor_binding_descs.size(), 1u);
        const auto &bindings = description.descriptor_binding_descs.front();
        const auto uses_stages = [&bindings](uint32_t binding,
                                             ShaderStage expected_stage)
        {
            const auto descriptor = std::find_if(
                bindings.begin(), bindings.end(),
                [binding](const graphics::DescriptorBindingDesc &candidate)
                { return candidate.binding == binding; });
            return descriptor != bindings.end() &&
                   descriptor->stage_flag == expected_stage;
        };
        EXPECT_TRUE(uses_stages(0, ShaderStage::SHADER_STAGE_VERTEX_FRAGMENT));
        EXPECT_TRUE(uses_stages(1, ShaderStage::SHADER_STAGE_VERTEX_FRAGMENT));
        resolver.Cleanup();
    }

    TEST(CaptureViewPassTest, DeclaresMotionVectorTextureBinding)
    {
        auto probe = std::make_shared<BackendProbe>();
        FakeBackend backend(probe);
        const auto prepared_assets = BuildPreparedCatalog();
        ASSERT_NE(prepared_assets, nullptr);

        render::CaptureViewPass pass;
        ASSERT_TRUE(pass.PrepareResources(backend, *prepared_assets));
        ASSERT_EQ(probe->pipelines.size(), 1u);
        ASSERT_EQ(probe->pipelines.front().descriptor_binding_descs.size(), 1u);
        const auto &bindings = probe->pipelines.front().descriptor_binding_descs.front();
        const auto motion = std::find_if(
            bindings.begin(), bindings.end(),
            [](const graphics::DescriptorBindingDesc &binding)
            { return binding.binding == 13; });
        ASSERT_NE(motion, bindings.end());
        EXPECT_EQ(motion->descriptor_type,
                  graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER);
        EXPECT_EQ(motion->stage_flag, ShaderStage::SHADER_STAGE_FRAGMENT);
        pass.Cleanup(backend);
    }

    struct InitFixtures
    {
        EventDispatcher<ResizeEvent> resize_dispatcher;
        int native_window_token = 0;
        std::vector<asset::AssetID> extra_textures;

        render::RenderSystemInitInfo Info(const render::RenderBackendFactory &factory)
        {
            render::RenderSystemInitInfo info{};
            info.api_type = GraphicsAPIType::GRAPHICS_API_OPENGL;
            info.native_window = &native_window_token;
            info.resize_dispatcher = &resize_dispatcher;
            info.prepared_assets = BuildPreparedCatalog(extra_textures);
            info.backend_factory = factory;
            return info;
        }
    };

    asset::AssetID RegisterEnvironmentTexture(const char *path, TextureFormat format,
                                              bool malformed = false)
    {
        auto texture_resource = std::make_shared<asset::TextureResource>();
        texture_resource->data->width = 4;
        texture_resource->data->height = 2;
        texture_resource->data->format = format;
        texture_resource->data->pixels.resize(
            malformed ? 1u : 4u * 2u * 4u * sizeof(uint16_t), 0);

        asset::AssetRegisterInfo texture_info{};
        texture_info.resource = texture_resource;
        texture_info.path = path;
        texture_info.name = path;
        texture_info.type = asset::AssetType::KPAT_Texture;
        return asset::AssetManager::GetInstance().RegisterAsset(texture_info);
    }

    render::RenderProfileSnapshot RecordDirectionalShadowCacheFrame(
        render::DeferredRenderer &renderer, graphics::RenderBackend &backend,
        render::FrameContext &frame_context, const render::RenderWorld &render_world,
        uint64_t frame_number, const Vector3f &camera_position,
        const Vector3f &light_direction)
    {
        render::RenderSceneFrameInput input{render_world};
        input.camera.SetPosition(camera_position);
        render::Light light{};
        light.handle = {42, 1};
        light.desc.type = render::LightType::Directional;
        light.desc.shadow = render::ShadowHandle{7, 1};
        light.desc.type_data = render::DirectionalLightData{light_direction};
        input.lights.push_back(light);
        input.is_shadow_handle_valid = [](render::ShadowHandle) { return true; };

        backend.BeginFrame();
        frame_context.Begin(backend.GetCurrentFrameIndex(),
                            {frame_number, 0.0f, 1.0f / 60.0f},
                            backend.GetRenderExtent());
        renderer.RecordFrame(frame_context, input);
        EXPECT_TRUE(renderer.ExecuteEditorCompositePass([] {}));
        EXPECT_TRUE(renderer.FinalizeFrame());
        frame_context.End();
        backend.EndFrame();
        return renderer.GetProfileSnapshot();
    }
}

TEST(SceneDrawRecorderTest, ReusesSectionPacketsUntilWorldRevisionChanges)
{
    auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    const auto prepared_assets = BuildPreparedCatalog();
    ASSERT_NE(prepared_assets, nullptr);
    render::RenderResourceResolver resolver(backend, *prepared_assets);
    render::SceneDrawRecorder recorder;

    render::MeshProxy visible{};
    visible.handle = {4u, 1u};
    visible.flags.visible = true;
    recorder.BeginFrame({visible}, 10u);
    const auto &first = recorder.BuildSectionCandidates(resolver);
    ASSERT_EQ(first.size(), 1u);
    const auto &cached = recorder.BuildSectionCandidates(resolver);
    EXPECT_EQ(&first, &cached);
    EXPECT_EQ(recorder.GetProfileCounters().section_packet_build_calls, 1u);

    render::MeshProxy hidden = visible;
    hidden.flags.visible = false;
    recorder.BeginFrame({hidden}, 11u);
    EXPECT_TRUE(recorder.BuildSectionCandidates(resolver).empty());
    EXPECT_EQ(recorder.GetProfileCounters().section_packet_build_calls, 1u);

    resolver.Cleanup();
}

TEST(FrameContextTest, ReusesStableBindingSetAndPublishesDynamicOffsets)
{
    auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    render::FrameContext frame;
    frame.Initialize(backend, 1024);
    frame.Begin(0, {1, 0.0f, 1.0f / 60.0f}, {320, 200});

    const graphics::PipelineHandle pipeline{1, 0};
    const render::UniformAllocation per_pass =
        frame.UpdateStableUniform(1, uint32_t{11});
    const render::UniformAllocation per_object =
        frame.UpdateStableUniform(2, uint32_t{22});
    ASSERT_TRUE(per_pass.IsValid());
    ASSERT_TRUE(per_object.IsValid());
    const std::vector<graphics::ResourceBinding> bindings{
        graphics::UniformBufferBinding{0, 0, per_pass.buffer, per_pass.offset, per_pass.range},
        graphics::UniformBufferBinding{0, 1, per_object.buffer, per_object.offset, per_object.range}};

    const render::FrameResourceBinding first =
        frame.CreateOrGetStableBindingSet(3, pipeline, bindings);
    const render::FrameResourceBinding second =
        frame.CreateOrGetStableBindingSet(3, pipeline, bindings);
    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(second.IsValid());
    EXPECT_EQ(first.descriptor_set, second.descriptor_set);
    EXPECT_EQ(first.dynamic_offsets, second.dynamic_offsets);
    EXPECT_EQ(first.dynamic_offsets.size(), 2u);
    EXPECT_EQ(probe->descriptor_set_create_count, 1);

    frame.Cleanup();
}

TEST(RenderSystemLifecycleTest, RejectsInvalidStateAndMakesShutdownIdempotent)
{
    render::RenderSystem system;
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::Uninitialized);
    EXPECT_FALSE(system.BeginFrame(1.0f / 60.0f));
    EXPECT_FALSE(system.EndFrame());
    EXPECT_FALSE(system.ExecuteEditorCompositePass([] {}));

    system.Shutdown();
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::ShutDown);
    system.Shutdown();
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::ShutDown);
}

TEST(RenderSystemLifecycleTest, SeparatesPresentationInitializationFromScenePromotion)
{
    auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    const render::RenderSystemInitInfo info = fixtures.Info(
        [probe](GraphicsAPIType) { return std::make_unique<FakeBackend>(probe); });

    ASSERT_TRUE(system.InitializePresentation(info));
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::PresentationReady);
    EXPECT_FALSE(system.GetSceneRenderTargetView().IsValid());
    EXPECT_EQ(system.GetMetrics().prepared_shader_count, 0U);
    EXPECT_EQ(system.GetMetrics().triangle_count, 0U);
    EXPECT_FALSE(system.GetMetrics().gpu_usage_percent.has_value());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::PresentationReady);

    ASSERT_TRUE(system.PromoteToScene(info.prepared_assets));
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::Ready);
    EXPECT_TRUE(system.GetSceneRenderTargetView().IsValid());
    system.Shutdown();
}

TEST(RenderSystemLifecycleTest, ReportsFailedCatalogPromotionAndAllowsRetry)
{
    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    const render::RenderSystemInitInfo info = fixtures.Info(
        [probe](GraphicsAPIType) { return std::make_unique<FakeBackend>(probe); });
    ASSERT_TRUE(system.Initialize(info));

    probe->fail_render_target_after = static_cast<int>(probe->targets.size());
    const uint64_t failed_serial =
        system.QueuePreparedAssetsUpdate(info.prepared_assets);
    ASSERT_NE(failed_serial, 0u);
    EXPECT_FALSE(system.BeginFrame(1.0f / 60.0f));
    const auto failed = system.GetPreparedAssetsUpdateResult(failed_serial);
    EXPECT_EQ(failed.status, render::PreparedAssetsUpdateStatus::Failed);
    EXPECT_FALSE(failed.diagnostic.empty());

    probe->fail_render_target_after = -1;
    const uint64_t retry_serial =
        system.QueuePreparedAssetsUpdate(info.prepared_assets);
    ASSERT_NE(retry_serial, 0u);
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    EXPECT_EQ(system.GetPreparedAssetsUpdateResult(retry_serial).status,
              render::PreparedAssetsUpdateStatus::Applied);
    EXPECT_TRUE(system.EndFrame());
    system.Shutdown();
}

TEST(RenderSystemLifecycleTest, SelectsViewportDebugTargetAtFrameBoundary)
{
    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    const graphics::RenderTargetView scene_view = system.GetDebugRenderTargetView();
    ASSERT_TRUE(scene_view.IsValid());
    EXPECT_EQ(system.GetDebugView(), render::CaptureView::SceneColor);

    system.SetDebugView(render::CaptureView::WorldNormal);
    // The request is intentionally deferred until BeginFrame, so the view
    // borrowed by the current editor composite remains frame-consistent.
    EXPECT_EQ(system.GetDebugView(), render::CaptureView::SceneColor);
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    const graphics::RenderTargetView debug_view = system.GetDebugRenderTargetView();
    ASSERT_TRUE(debug_view.IsValid());
    EXPECT_EQ(system.GetDebugView(), render::CaptureView::WorldNormal);
    EXPECT_EQ(system.GetSceneRenderTargetView().native_image_view, scene_view.native_image_view);
    EXPECT_NE(debug_view.native_image_view, scene_view.native_image_view);
    ASSERT_TRUE(system.EndFrame());

    system.SetDebugView(render::CaptureView::EngineWindow);
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    EXPECT_EQ(system.GetDebugView(), render::CaptureView::WorldNormal);
    ASSERT_TRUE(system.EndFrame());
    system.Shutdown();
}

TEST(RenderSystemLifecycleTest, DebugViewConsumersCanReleaseWithoutCancellingEachOther)
{
    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    system.SetDebugView(render::CaptureView::WorldNormal);
    system.SetDebugViewDemand(render::DebugViewConsumer::EditorDebugViewer,
                              render::CaptureView::BaseColor);
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    EXPECT_EQ(system.GetDebugView(), render::CaptureView::BaseColor);
    ASSERT_TRUE(system.EndFrame());

    system.SetDebugViewDemand(render::DebugViewConsumer::EditorDebugViewer, std::nullopt);
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    EXPECT_EQ(system.GetDebugView(), render::CaptureView::WorldNormal);
    ASSERT_TRUE(system.EndFrame());
    system.Shutdown();
}

TEST(RenderSystemLifecycleTest, AppliesCopiedPathTraceSettingsAtFrameBoundary)
{
    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    render::PathTraceSettings settings;
    settings.path_tracing_enabled = false;
    settings.samples_per_dispatch = 2;
    settings.maximum_continuation_bounces = 5;
    settings.reconstruction = render::PathTraceReconstruction::GuidedPreview;
    EXPECT_TRUE(system.RequestPathTraceSettings(settings));

    settings.samples_per_dispatch = 0;
    EXPECT_FALSE(system.RequestPathTraceSettings(settings));
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    const render::RenderProfileSnapshot profile = system.GetMetrics().profile;
    EXPECT_FALSE(profile.path_trace_settings_requested.path_tracing_enabled);
    EXPECT_EQ(profile.path_trace_settings_requested.samples_per_dispatch, 2U);
    EXPECT_EQ(profile.path_trace_settings_requested.maximum_continuation_bounces, 5U);
    EXPECT_EQ(profile.path_trace_settings_requested.reconstruction,
              render::PathTraceReconstruction::GuidedPreview);
    ASSERT_TRUE(system.EndFrame());
    system.Shutdown();
}

TEST(RenderSystemLifecycleTest, RollsBackWhenARequiredCollaboratorFails)
{
    const auto probe = std::make_shared<BackendProbe>();
    probe->fail_uniform_mapping = true;
    InitFixtures fixtures;
    render::RenderSystem system;
    const render::RenderSystemInitResult result = system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); }));

    EXPECT_FALSE(result);
    EXPECT_NE(result.diagnostic.find("frame uniform allocator"), std::string::npos);
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::Uninitialized);
    EXPECT_EQ(probe->cleanup_count, 1);
    EXPECT_NE(std::find(probe->events.begin(), probe->events.end(), "backend_cleanup"),
              probe->events.end());
}

TEST(RenderSystemLifecycleTest, KeepsSourceSinkAddressesStableAcrossRetry)
{
    const auto probe = std::make_shared<BackendProbe>();
    probe->fail_uniform_mapping = true;
    InitFixtures fixtures;
    render::RenderSystem system;
    auto *const renderable_sink = system.GetRenderableSourceSink();
    auto *const light_sink = system.GetLightSourceSink();
    auto *const camera_sink = system.GetCameraSourceSink();
    auto *const environment_sink = system.GetEnvironmentSourceSink();

    EXPECT_FALSE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));
    EXPECT_EQ(system.GetRenderableSourceSink(), renderable_sink);
    EXPECT_EQ(system.GetLightSourceSink(), light_sink);
    EXPECT_EQ(system.GetCameraSourceSink(), camera_sink);
    EXPECT_EQ(system.GetEnvironmentSourceSink(), environment_sink);

    probe->fail_uniform_mapping = false;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));
    EXPECT_EQ(system.GetRenderableSourceSink(), renderable_sink);
    EXPECT_EQ(system.GetLightSourceSink(), light_sink);
    EXPECT_EQ(system.GetCameraSourceSink(), camera_sink);
    EXPECT_EQ(system.GetEnvironmentSourceSink(), environment_sink);
    system.Shutdown();
}

TEST(RenderSystemLifecycleTest, UnwindsBackendFrameWhenNoRecorderIsAvailable)
{
    const auto probe = std::make_shared<BackendProbe>();
    probe->missing_command_recorder = true;
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    EXPECT_FALSE(system.BeginFrame(1.0f / 60.0f));
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::Ready);
    EXPECT_FALSE(system.EndFrame());
    EXPECT_EQ(std::count(probe->events.begin(), probe->events.end(), "begin_frame"), 1);
    EXPECT_EQ(std::count(probe->events.begin(), probe->events.end(), "end_frame"), 1);
    system.Shutdown();
}

TEST(RenderSystemLifecycleTest, RendererTargetInitializationRollsBackPartialOwnership)
{
    const auto probe = std::make_shared<BackendProbe>();
    probe->fail_render_target_after = 3;
    InitFixtures fixtures;
    render::RenderSystem system;
    const render::RenderSystemInitResult result = system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); }));

    EXPECT_FALSE(result);
    EXPECT_EQ(probe->render_target_destroy_count, 3);
    EXPECT_EQ(probe->cleanup_count, 1);
    const auto destroy_it =
        std::find(probe->events.begin(), probe->events.end(), "destroy_target");
    const auto cleanup_it =
        std::find(probe->events.begin(), probe->events.end(), "backend_cleanup");
    ASSERT_NE(destroy_it, probe->events.end());
    ASSERT_NE(cleanup_it, probe->events.end());
    EXPECT_LT(destroy_it, cleanup_it);
}

TEST(RenderSystemLifecycleTest, FullscreenMeshSurvivesSamplerRetryFailure)
{
    const auto probe = std::make_shared<BackendProbe>();
    probe->fail_sampler_creation = true;
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                       { return std::make_unique<FakeBackend>(probe); })));

    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    // A failed required lighting producer blocks the frame's output.
    EXPECT_FALSE(system.EndFrame());
    system.Shutdown();

    // The normal frame asks for the fullscreen pair from both deferred
    // lighting and tone mapping. A failed sampler must not recreate the mesh.
    EXPECT_EQ(probe->mesh_create_count, 1);
    EXPECT_EQ(probe->mesh_destroy_count, 1);
    EXPECT_EQ(probe->sampler_destroy_count, 0);
}

TEST(RenderSystemLifecycleTest, FullscreenSamplerSurvivesMeshRetryFailure)
{
    const auto probe = std::make_shared<BackendProbe>();
    probe->fail_mesh_creation = true;
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                       { return std::make_unique<FakeBackend>(probe); })));

    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    // A failed required lighting producer blocks the frame's output.
    EXPECT_FALSE(system.EndFrame());
    system.Shutdown();

    // The mesh failure is retried by both consumers, but the successful
    // sampler remains the renderer-owned resource for cleanup.
    EXPECT_EQ(probe->sampler_create_count, 1);
    EXPECT_EQ(probe->sampler_destroy_count, 1);
}

TEST(DeferredRendererTest, OwnsTargetLifetimeAndCleanupIsIdempotent)
{
    const auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    render::MaterialSystem materials;
    const auto prepared_assets = BuildPreparedCatalog();
    ASSERT_NE(prepared_assets, nullptr);
    render::RenderResourceResolver resolver(backend, *prepared_assets);
    render::DeferredRenderer renderer;

    ASSERT_TRUE(renderer.Initialize({backend, resolver, materials, *prepared_assets}, 320, 200));
    EXPECT_TRUE(renderer.IsFramePlanValid());
    renderer.Cleanup();
    renderer.Cleanup();

    EXPECT_EQ(probe->render_target_destroy_count,
              static_cast<int>(probe->targets.size()));
    EXPECT_EQ(probe->cleanup_count, 0);
    resolver.Cleanup();
}

TEST(DeferredRendererTest, ReusesDirectionalShadowWhenCameraStaysInsideEffectiveFit)
{
    const auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    render::MaterialSystem materials;
    const auto prepared_assets = BuildPreparedCatalog();
    ASSERT_NE(prepared_assets, nullptr);
    render::RenderResourceResolver resolver(backend, *prepared_assets);
    render::DeferredRenderer renderer;
    ASSERT_TRUE(renderer.Initialize({backend, resolver, materials, *prepared_assets}, 320, 200));

    render::RenderWorld render_world;
    render::MeshProxyDesc proxy{};
    proxy.world_bounds = {{-10.0f, -10.0f, -10.0f}, {10.0f, 10.0f, 10.0f}};
    proxy.flags.visible = true;
    proxy.flags.casts_shadow = true;
    render_world.EnqueueCreate(proxy);
    render_world.ApplyPendingCommands();

    render::FrameContext frame_context;
    frame_context.Initialize(backend, 1024 * 1024);
    const Vector3f light_direction{0.0f, -1.0f, 0.0f};
    const auto first = RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 1, {0.0f, 0.0f, 2.0f}, light_direction);
    // The Editor submits the current viewport extent every UI frame. Repeating
    // an unchanged request must not invalidate the shadow cache.
    renderer.RequestExtent(320, 200);
    renderer.ApplyPendingExtent();
    const auto second = RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 2, {1.0f, 0.0f, 2.0f}, light_direction);

    EXPECT_EQ(first.shadow_cache_hits, 0U);
    EXPECT_EQ(first.shadow_cache_misses, 1U);
    EXPECT_EQ(second.shadow_cache_hits, 1U);
    EXPECT_EQ(second.shadow_cache_misses, 0U);

    frame_context.Cleanup();
    renderer.Cleanup();
    resolver.Cleanup();
}

TEST(DeferredRendererTest, InvalidatesDirectionalShadowWhenCameraLeavesEffectiveFit)
{
    const auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    render::MaterialSystem materials;
    const auto prepared_assets = BuildPreparedCatalog();
    ASSERT_NE(prepared_assets, nullptr);
    render::RenderResourceResolver resolver(backend, *prepared_assets);
    render::DeferredRenderer renderer;
    ASSERT_TRUE(renderer.Initialize({backend, resolver, materials, *prepared_assets}, 320, 200));

    render::RenderWorld render_world;
    render::MeshProxyDesc proxy{};
    proxy.world_bounds = {{-10.0f, -10.0f, -10.0f}, {10.0f, 10.0f, 10.0f}};
    proxy.flags.visible = true;
    proxy.flags.casts_shadow = true;
    render_world.EnqueueCreate(proxy);
    render_world.ApplyPendingCommands();

    render::FrameContext frame_context;
    frame_context.Initialize(backend, 1024 * 1024);
    const Vector3f light_direction{0.0f, -1.0f, 0.0f};
    RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 1, {0.0f, 0.0f, 2.0f}, light_direction);
    const auto moved_outside = RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 2, {40.0f, 0.0f, 2.0f}, light_direction);

    EXPECT_EQ(moved_outside.shadow_cache_hits, 0U);
    EXPECT_EQ(moved_outside.shadow_cache_misses, 1U);

    frame_context.Cleanup();
    renderer.Cleanup();
    resolver.Cleanup();
}

TEST(DeferredRendererTest, InvalidatesDirectionalShadowWhenLightOrCasterChanges)
{
    const auto probe = std::make_shared<BackendProbe>();
    FakeBackend backend(probe);
    render::MaterialSystem materials;
    const auto prepared_assets = BuildPreparedCatalog();
    ASSERT_NE(prepared_assets, nullptr);
    render::RenderResourceResolver resolver(backend, *prepared_assets);
    render::DeferredRenderer renderer;
    ASSERT_TRUE(renderer.Initialize({backend, resolver, materials, *prepared_assets}, 320, 200));

    render::RenderWorld render_world;
    render::MeshProxyDesc proxy{};
    proxy.world_bounds = {{-10.0f, -10.0f, -10.0f}, {10.0f, 10.0f, 10.0f}};
    proxy.flags.visible = true;
    proxy.flags.casts_shadow = true;
    const render::RenderableHandle proxy_handle = render_world.EnqueueCreate(proxy);
    render_world.ApplyPendingCommands();

    render::FrameContext frame_context;
    frame_context.Initialize(backend, 1024 * 1024);
    const Vector3f initial_direction{0.0f, -1.0f, 0.0f};
    const auto first = RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 1, {0.0f, 0.0f, 2.0f}, initial_direction);
    const auto light_changed = RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 2, {0.0f, 0.0f, 2.0f},
        {1.0f, -1.0f, 0.0f});

    proxy.world_bounds = {{-20.0f, -10.0f, -10.0f}, {10.0f, 10.0f, 10.0f}};
    ASSERT_TRUE(render_world.EnqueueUpdate(proxy_handle, proxy));
    render_world.ApplyPendingCommands();
    const auto caster_changed = RecordDirectionalShadowCacheFrame(
        renderer, backend, frame_context, render_world, 3, {0.0f, 0.0f, 2.0f},
        {1.0f, -1.0f, 0.0f});

    EXPECT_EQ(first.shadow_cache_misses, 1U);
    EXPECT_EQ(light_changed.shadow_cache_hits, 0U);
    EXPECT_EQ(light_changed.shadow_cache_misses, 1U);
    EXPECT_EQ(caster_changed.shadow_cache_hits, 0U);
    EXPECT_EQ(caster_changed.shadow_cache_misses, 1U);

    frame_context.Cleanup();
    renderer.Cleanup();
    resolver.Cleanup();
}

TEST(RenderSystemLifecycleTest, CharacterizesFrameCaptureResizeEditorAndTeardownOrder)
{
    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::Ready);

    bool capture_completed = false;
    ASSERT_TRUE(system.GetRenderCaptureService()->RequestCapture(
        {render::CaptureView::SceneColor},
        [&capture_completed](render::CaptureResult result)
        { capture_completed = result.IsSuccess(); }));

    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::FrameActive);
    EXPECT_FALSE(system.BeginFrame(1.0f / 60.0f));

    bool editor_recorded = false;
    EXPECT_TRUE(system.ExecuteEditorCompositePass(
        [&editor_recorded]
        {
            editor_recorded = true;
        }));
    EXPECT_TRUE(editor_recorded);
    EXPECT_FALSE(system.ExecuteEditorCompositePass([] {}));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::Ready);
    EXPECT_TRUE(capture_completed);
    EXPECT_EQ(probe->readback_count, 1);

    const std::vector<std::string> expected_targets{
        "target:DirectionalShadow", "target:SpotShadow", "target:PointShadow",
        // SceneHdr is deliberately absent: its contents never survive the frame,
        // so it is a transient the Graphics-owned pool allocates rather than one
        // of the persistent named targets.
        "target:GBuffer", "target:SceneColor"};
    std::vector<std::string> actual_targets;
    for (const std::string &event : probe->events)
    {
        if (event.rfind("target:", 0) == 0)
        {
            actual_targets.push_back(event);
        }
    }
    EXPECT_EQ(actual_targets, expected_targets);

    const graphics::RenderTargetView view_before_resize = system.GetSceneRenderTargetView();
    ASSERT_TRUE(view_before_resize.IsValid());
    system.RequestSceneRenderTargetExtent(640, 360);
    const int waits_before_resize = probe->wait_idle_count;
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_GT(probe->wait_idle_count, waits_before_resize);
    // Two initializations: find the resized SceneColor target by its extent,
    // since target creation order is an implementation detail of the fixture.
    const auto resized_scene_color = std::find_if(
        probe->targets.begin(), probe->targets.end(), [](const TargetRecord &target)
        { return target.name == "SceneColor" && target.width == 640u && target.height == 360u; });
    ASSERT_NE(resized_scene_color, probe->targets.end());
    // SceneHdr is taken from the pool once per frame and returned after the
    // sweep, so the transient path is exercised rather than merely present.
    EXPECT_EQ(std::count(probe->events.begin(), probe->events.end(), "transient_acquire"),
              std::count(probe->events.begin(), probe->events.end(), "transient_release"));

    const graphics::RenderTargetView view_after_resize = system.GetSceneRenderTargetView();
    ASSERT_TRUE(view_after_resize.IsValid());
    EXPECT_EQ(view_after_resize.width, 640u);
    EXPECT_EQ(view_after_resize.height, 360u);
    EXPECT_NE(view_before_resize.native_image_view, view_after_resize.native_image_view);
    // The old value is borrowed and must not be reused after the extent change;
    // the Editor reacquires the replacement view on its next draw.

    system.Shutdown();
    EXPECT_EQ(system.GetLifecycleState(), render::RenderSystemLifecycleState::ShutDown);
    EXPECT_EQ(probe->cleanup_count, 1);
    const auto wait_it = std::find(probe->events.begin(), probe->events.end(), "wait_idle");
    const auto cleanup_it =
        std::find(probe->events.begin(), probe->events.end(), "backend_cleanup");
    ASSERT_NE(wait_it, probe->events.end());
    ASSERT_NE(cleanup_it, probe->events.end());
    EXPECT_LT(wait_it, cleanup_it);
    EXPECT_EQ(probe->render_target_destroy_count,
              static_cast<int>(probe->targets.size()));
    const auto last_destroy_it =
        std::find(probe->events.rbegin(), probe->events.rend(), "destroy_target");
    ASSERT_NE(last_destroy_it, probe->events.rend());
    EXPECT_LT(last_destroy_it.base() - 1, cleanup_it);

    EXPECT_EQ(probe->pipeline_create_count, probe->pipeline_destroy_count);
    EXPECT_EQ(probe->mesh_create_count, probe->mesh_destroy_count);
    EXPECT_EQ(probe->sampler_create_count, probe->sampler_destroy_count);
    for (const char *event_name : {"destroy_pipeline", "destroy_mesh", "destroy_sampler"})
    {
        const auto last_destroy =
            std::find(probe->events.rbegin(), probe->events.rend(), event_name);
        ASSERT_NE(last_destroy, probe->events.rend()) << event_name;
        EXPECT_LT(last_destroy.base() - 1, cleanup_it) << event_name;
    }
}

TEST(RenderSystemEnvironmentTest, ResolvesReadyAssetIDsAndReusesDerivedBindings)
{
    auto texture_resource = std::make_shared<asset::TextureResource>();
    texture_resource->data->width = 4;
    texture_resource->data->height = 2;
    texture_resource->data->format = TextureFormat::TEXTURE_FORMAT_RGBA16F;
    texture_resource->data->pixels.resize(4 * 2 * 4 * sizeof(uint16_t), 0);

    asset::AssetRegisterInfo texture_info{};
    texture_info.resource = texture_resource;
    texture_info.path = "render_system_environment_test.texture";
    texture_info.name = "RenderSystemEnvironmentTest";
    texture_info.type = asset::AssetType::KPAT_Texture;
    asset::AssetManager &asset_manager = asset::AssetManager::GetInstance();
    const asset::AssetID texture_id = asset_manager.RegisterAsset(texture_info);
    ASSERT_TRUE(texture_id.IsValid());

    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    fixtures.extra_textures.push_back(texture_id);
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    const render::EnvironmentSourceDesc first_source{texture_id, 1.0f};
    const auto first = system.GetEnvironmentSourceSink()->EnqueueCreate(first_source);
    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    const int first_frame_texture_creates = probe->texture_create_count;
    EXPECT_GT(first_frame_texture_creates, 0);

    ASSERT_TRUE(system.GetEnvironmentSourceSink()->EnqueueDestroy(first));
    const render::EnvironmentSourceDesc second_source{texture_id, 2.0f};
    const auto second = system.GetEnvironmentSourceSink()->EnqueueCreate(second_source);
    ASSERT_TRUE(second.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(probe->texture_create_count, first_frame_texture_creates);

    ASSERT_TRUE(system.GetEnvironmentSourceSink()->EnqueueDestroy(second));
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    system.Shutdown();
    asset_manager.UnRegisterAsset(texture_id);
}

TEST(RenderSystemEnvironmentTest, RetainsBaselineAcrossTypedResolutionFailures)
{
    asset::AssetManager &asset_manager = asset::AssetManager::GetInstance();
    const asset::AssetID wrong_format_id = RegisterEnvironmentTexture(
        "render_system_environment_wrong_format.texture",
        TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB);
    const asset::AssetID malformed_id = RegisterEnvironmentTexture(
        "render_system_environment_malformed.texture",
        TextureFormat::TEXTURE_FORMAT_RGBA16F, true);
    const asset::AssetID valid_id = RegisterEnvironmentTexture(
        "render_system_environment_backend_failure.texture",
        TextureFormat::TEXTURE_FORMAT_RGBA16F);
    ASSERT_TRUE(wrong_format_id.IsValid());
    ASSERT_TRUE(malformed_id.IsValid());
    ASSERT_TRUE(valid_id.IsValid());

    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    fixtures.extra_textures = {wrong_format_id, malformed_id, valid_id};
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    ASSERT_FALSE(probe->environment_binding_snapshots.empty());
    const auto baseline = probe->environment_binding_snapshots.back();

    const auto wrong_format = system.GetEnvironmentSourceSink()->EnqueueCreate(
        {wrong_format_id, 1.0f});
    ASSERT_TRUE(wrong_format.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(probe->environment_binding_snapshots.back(), baseline);

    const std::vector<kpengine::program::LogEntry> logs_before_repeat =
        kpengine::LogSystem{}.GetLogSnapshot();
    const std::size_t unresolved_diagnostics = std::count_if(
        logs_before_repeat.begin(), logs_before_repeat.end(),
        [](const kpengine::program::LogEntry &entry)
        {
            return entry.name == "RenderLog" &&
                   entry.message ==
                       "Level environment source could not be resolved; retaining baseline";
        });
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    const std::vector<kpengine::program::LogEntry> logs_after_repeat =
        kpengine::LogSystem{}.GetLogSnapshot();
    const std::size_t repeated_unresolved_diagnostics = std::count_if(
        logs_after_repeat.begin(), logs_after_repeat.end(),
        [](const kpengine::program::LogEntry &entry)
        {
            return entry.name == "RenderLog" &&
                   entry.message ==
                       "Level environment source could not be resolved; retaining baseline";
        });
    EXPECT_EQ(repeated_unresolved_diagnostics, unresolved_diagnostics);

    ASSERT_TRUE(system.GetEnvironmentSourceSink()->EnqueueDestroy(wrong_format));
    const auto malformed = system.GetEnvironmentSourceSink()->EnqueueCreate(
        {malformed_id, 1.0f});
    ASSERT_TRUE(malformed.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(probe->environment_binding_snapshots.back(), baseline);

    ASSERT_TRUE(system.GetEnvironmentSourceSink()->EnqueueDestroy(malformed));
    probe->fail_texture_creation = true;
    const auto backend_failure = system.GetEnvironmentSourceSink()->EnqueueCreate(
        {valid_id, 1.0f});
    ASSERT_TRUE(backend_failure.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(probe->environment_binding_snapshots.back(), baseline);
    EXPECT_GT(probe->texture_create_count, 0);

    system.Shutdown();
    asset_manager.UnRegisterAsset(wrong_format_id);
    asset_manager.UnRegisterAsset(malformed_id);
    asset_manager.UnRegisterAsset(valid_id);
}

TEST(RenderSystemEnvironmentTest, KeepsBaselineWhenLevelSourceFails)
{
    asset::AssetManager &asset_manager = asset::AssetManager::GetInstance();
    const asset::AssetID invalid_id = RegisterEnvironmentTexture(
        "render_system_environment_bootstrap_invalid.texture",
        TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB);
    ASSERT_TRUE(invalid_id.IsValid());

    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType) { return std::make_unique<FakeBackend>(probe); })));

    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    ASSERT_FALSE(probe->environment_binding_snapshots.empty());
    const auto baseline = probe->environment_binding_snapshots.back();

    const auto invalid = system.GetEnvironmentSourceSink()->EnqueueCreate({invalid_id, 1.0f});
    ASSERT_TRUE(invalid.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(probe->environment_binding_snapshots.back(), baseline);

    system.Shutdown();
    asset_manager.UnRegisterAsset(invalid_id);
}

TEST(RenderSystemEnvironmentTest, PublishesEnvironmentBindingsAtomicallyAndRestoresBaseline)
{
    asset::AssetManager &asset_manager = asset::AssetManager::GetInstance();
    const asset::AssetID texture_id = RegisterEnvironmentTexture(
        "render_system_environment_atomic.texture", TextureFormat::TEXTURE_FORMAT_RGBA16F);
    ASSERT_TRUE(texture_id.IsValid());

    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    fixtures.extra_textures.push_back(texture_id);
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    ASSERT_FALSE(probe->environment_binding_snapshots.empty());
    const auto baseline = probe->environment_binding_snapshots.back();

    const auto source = system.GetEnvironmentSourceSink()->EnqueueCreate({texture_id, 2.0f});
    ASSERT_TRUE(source.IsValid());
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    ASSERT_FALSE(probe->environment_binding_snapshots.empty());
    const auto resolved = probe->environment_binding_snapshots.back();
    EXPECT_NE(resolved, baseline);
    EXPECT_TRUE(std::all_of(resolved.begin(), resolved.end(),
                            [](uint32_t id) { return id != KPENGINE_NULL_HANDLE; }));

    ASSERT_TRUE(system.GetEnvironmentSourceSink()->EnqueueDestroy(source));
    ASSERT_TRUE(system.BeginFrame(1.0f / 60.0f));
    ASSERT_TRUE(system.EndFrame());
    EXPECT_EQ(probe->environment_binding_snapshots.back(), baseline);

    system.Shutdown();
    asset_manager.UnRegisterAsset(texture_id);
}

TEST(RenderSystemEnvironmentTest, ClearsEnvironmentSourceHandlesDuringShutdown)
{
    const asset::AssetID texture_id = RegisterEnvironmentTexture(
        "render_system_environment_shutdown.texture", TextureFormat::TEXTURE_FORMAT_RGBA16F);
    ASSERT_TRUE(texture_id.IsValid());

    const auto probe = std::make_shared<BackendProbe>();
    InitFixtures fixtures;
    render::RenderSystem system;
    ASSERT_TRUE(system.Initialize(
        fixtures.Info([probe](GraphicsAPIType)
                      { return std::make_unique<FakeBackend>(probe); })));

    const auto handle =
        system.GetEnvironmentSourceSink()->EnqueueCreate({texture_id, 1.0f});
    ASSERT_TRUE(handle.IsValid());
    system.Shutdown();

    EXPECT_FALSE(system.GetEnvironmentSourceSink()->EnqueueDestroy(handle));
    const auto replacement =
        system.GetEnvironmentSourceSink()->EnqueueCreate({texture_id, 1.0f});
    ASSERT_TRUE(replacement.IsValid());
    EXPECT_FALSE(replacement == handle);
    EXPECT_TRUE(system.GetEnvironmentSourceSink()->EnqueueDestroy(replacement));
    asset::AssetManager::GetInstance().UnRegisterAsset(texture_id);
}
