#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "render/render_graph/render_graph.h"
#include "render/render_pass_declaration.h"

namespace
{
    using kpengine::render::CompiledRenderGraph;
    using kpengine::render::CompiledRenderFramePlan;
    using kpengine::render::CompileRenderFramePlan;
    using kpengine::render::CompileRenderFrameGraph;
    using kpengine::render::FixedRenderPassEntry;
    using kpengine::render::FixedRenderPassId;
    using kpengine::render::GetRenderFramePassEntries;
    using kpengine::render::GraphTextureHandle;
    using kpengine::render::GraphAccelerationStructureHandle;
    using kpengine::render::RenderFrameConditions;
    using kpengine::render::RenderFrameGraphResourceHandle;
    using kpengine::render::RenderFrameResourceImport;
    using kpengine::render::RenderFrameResourceRole;
    using kpengine::render::RenderGraphCompileResult;
    using kpengine::render::RenderGraphAccess;
    using kpengine::render::RenderGraphPassOwner;
    using kpengine::render::RenderGraphResourceUse;
    using kpengine::render::RenderPassAccess;
    using kpengine::render::RenderPassCondition;
    using kpengine::render::RenderPassExecutionOwner;
    using kpengine::render::RenderPassResource;
    using kpengine::render::RenderPassResourceUse;

    constexpr std::size_t kResourceCount =
        static_cast<std::size_t>(RenderPassResource::Count);

    // The passes the conditions schedule: everything except a pass whose
    // condition is not met. The compiled plan culls exactly those.
    std::vector<std::string> ExpectedPlannedPasses(RenderFrameConditions conditions)
    {
        std::vector<std::string> names;
        for (const FixedRenderPassEntry &entry : GetRenderFramePassEntries())
        {
            const bool skipped =
                (entry.condition == RenderPassCondition::DiagnosticCaptureRequested &&
                 !conditions.diagnostic_capture) ||
                (entry.condition == RenderPassCondition::DebugViewRequested &&
                 !conditions.debug_view) ||
                (entry.condition == RenderPassCondition::RasterDiagnostic &&
                 conditions.ray_tracing_path_trace && !conditions.diagnostic_capture &&
                 !conditions.debug_view) ||
                (entry.condition == RenderPassCondition::RasterFrame &&
                 conditions.ray_tracing_path_trace) ||
                (entry.condition == RenderPassCondition::HeightFogEnabled &&
                 (!conditions.height_fog || conditions.ray_tracing_path_trace)) ||
                (entry.condition == RenderPassCondition::RayTracingPathTrace &&
                 !conditions.ray_tracing_path_trace) ||
                (entry.condition == RenderPassCondition::RayTracingBuildRequested &&
                 ((entry.id == FixedRenderPassId::RayTracingBlasBuild &&
                   !conditions.ray_tracing_blas_build) ||
                  (entry.id == FixedRenderPassId::RayTracingTlasBuild &&
                   !conditions.ray_tracing_tlas_build)));
            if (!skipped)
            {
                names.push_back(entry.name);
            }
        }
        return names;
    }

    bool IsResourceUseEnabled(const RenderPassResourceUse &use,
                              RenderFrameConditions conditions)
    {
        switch (use.condition)
        {
        case RenderPassCondition::ScreenSpaceAoEnabled:
            return conditions.screen_space_ao && !conditions.ray_tracing_path_trace;
        case RenderPassCondition::ScreenSpaceAoDiagnosticRequested:
            return conditions.screen_space_ao_diagnostic && conditions.screen_space_ao &&
                   !conditions.ray_tracing_path_trace;
        case RenderPassCondition::TemporalAaEnabled:
            return conditions.temporal_aa && !conditions.ray_tracing_path_trace;
        case RenderPassCondition::TemporalAaDisabled:
            return !conditions.temporal_aa && !conditions.ray_tracing_path_trace;
        case RenderPassCondition::HeightFogEnabled:
            return conditions.height_fog && !conditions.ray_tracing_path_trace;
        case RenderPassCondition::HeightFogDisabled:
            return !conditions.height_fog && !conditions.ray_tracing_path_trace;
        case RenderPassCondition::RasterToneMapFogged:
            return conditions.height_fog && !conditions.temporal_aa && !conditions.ray_tracing_path_trace;
        case RenderPassCondition::RasterToneMapUnfogged:
            return !conditions.height_fog && !conditions.temporal_aa && !conditions.ray_tracing_path_trace;
        default:
            return true;
        }
    }
}

TEST(RenderGraphCompatibilityTest, ProductionDeclarationDerivesTransitionIntents)
{
    const auto result = CompileRenderFrameGraph(RenderFrameConditions{false});
    ASSERT_TRUE(result.Succeeded());

    // Assert semantic transitions without freezing the count; optional AO/TAA
    // consumers add valid transitions as their graph branches are enabled.
    const auto &transitions = result.graph->Transitions();
    ASSERT_FALSE(transitions.empty());
    for (const auto &transition : transitions)
    {
        EXPECT_TRUE(transition.handle.index() == 0);
        EXPECT_FALSE(transition.resource_name.empty());
    }

    using kpengine::render::RenderGraphAttachmentScope;
    using kpengine::render::RenderGraphUsage;
    const auto expect = [&transitions](const char *name, RenderGraphUsage usage,
                                       RenderGraphAttachmentScope scope = {}) {
        const auto transition = std::find_if(
            transitions.begin(), transitions.end(), [&](const auto &candidate) {
                return candidate.resource_name == name && candidate.usage == usage &&
                       candidate.scope == scope;
            });
        EXPECT_NE(transition, transitions.end()) << name;
        return transition;
    };
    // Shadows are written as depth attachments and read sampled afterwards.
    expect("DirectionalShadow", RenderGraphUsage::DepthAttachment);
    expect("SpotShadow", RenderGraphUsage::DepthAttachment);
    expect("PointShadow", RenderGraphUsage::DepthAttachment);
    // The G-buffer is written whole, then read by two passes that touch
    // different attachments, so each states its own requirement.
    expect("GBuffer", RenderGraphUsage::ColorAttachment, RenderGraphAttachmentScope::Whole());
    expect("GBuffer", RenderGraphUsage::Sampled,
           RenderGraphAttachmentScope::Colors(0b0111U, true));
    expect("GBuffer", RenderGraphUsage::Sampled,
           RenderGraphAttachmentScope::Colors(0b110010U));
    expect("SceneHdr", RenderGraphUsage::ColorAttachment);
    expect("SceneHdr", RenderGraphUsage::Sampled);
    const auto selection = expect("GBuffer", RenderGraphUsage::Sampled,
                                  RenderGraphAttachmentScope::Colors(0b1000U));
    ASSERT_NE(selection, transitions.end());
    const auto tone_map = std::find_if(
        result.graph->Passes().begin(), result.graph->Passes().end(),
        [](const auto &pass) { return pass.name == "ToneMapPass"; });
    ASSERT_NE(tone_map, result.graph->Passes().end());
    EXPECT_EQ(selection->pass_index,
              static_cast<uint32_t>(tone_map - result.graph->Passes().begin()));
    expect("SceneColor", RenderGraphUsage::ColorAttachment);
    expect("SceneColor", RenderGraphUsage::Sampled);
}

TEST(RenderGraphCompatibilityTest, ProductionDeclarationPlansSceneHdrAsATransient)
{
    const auto result = CompileRenderFrameGraph(RenderFrameConditions{false});
    ASSERT_TRUE(result.Succeeded());

    // SceneHdr is written by deferred lighting and read by tone map, both inside
    // the frame, so the plan asks for a physical object for that window and only
    // that window. Everything else is a persistent target the renderer owns.
    const auto &transients = result.graph->Transients();
    ASSERT_EQ(transients.size(), 3U);
    const auto expect_transient = [&](const char *name, uint64_t key,
                                      const char *first_pass, const char *last_pass) {
        const auto transient = std::find_if(
            transients.begin(), transients.end(), [&](const auto &candidate) {
                return candidate.name == name;
            });
        ASSERT_NE(transient, transients.end());
        EXPECT_EQ(transient->key, key);
        ASSERT_LT(transient->first_use, result.graph->Passes().size());
        ASSERT_LT(transient->last_use, result.graph->Passes().size());
        EXPECT_EQ(result.graph->Passes()[transient->first_use].name, first_pass);
        EXPECT_EQ(result.graph->Passes()[transient->last_use].name, last_pass);
    };
    expect_transient("SceneHdr",
                     static_cast<uint64_t>(kpengine::render::RenderFrameTransient::SceneHdr),
                     "DeferredLightingPass", "TemporalAaResolvePass");
    expect_transient("ScreenSpaceAoRaw",
                     static_cast<uint64_t>(kpengine::render::RenderFrameTransient::ScreenSpaceAoRaw),
                     "ScreenSpaceAoEstimatePass", "ScreenSpaceAoFilterPass");
    expect_transient("ScreenSpaceAoFiltered",
                     static_cast<uint64_t>(kpengine::render::RenderFrameTransient::ScreenSpaceAoFiltered),
                     "ScreenSpaceAoFilterPass", "DeferredLightingPass");
}

TEST(RenderGraphCompatibilityTest, RayQueryVariantImportsTheGraphicsOwnedTlas)
{
    const auto result = CompileRenderFrameGraph(RenderFrameConditions{false, true});
    ASSERT_TRUE(result.Succeeded());

    const auto deferred_lighting = std::find_if(
        result.graph->Passes().begin(), result.graph->Passes().end(),
        [](const CompiledRenderGraph::Pass &pass) {
            return pass.name == "DeferredLightingPass";
        });
    ASSERT_NE(deferred_lighting, result.graph->Passes().end());
    const auto tlas_use = std::find_if(
        deferred_lighting->uses.begin(), deferred_lighting->uses.end(),
        [](const RenderGraphResourceUse &use) {
            return std::holds_alternative<kpengine::render::GraphAccelerationStructureHandle>(
                       use.handle) &&
                   use.usage == kpengine::render::RenderGraphUsage::AccelerationStructureRead;
        });
    ASSERT_NE(tlas_use, deferred_lighting->uses.end());
    EXPECT_EQ(tlas_use->stage, kpengine::render::RenderGraphStage::RayTracingShader);
    EXPECT_TRUE(std::any_of(
        result.graph->Lifetimes().begin(), result.graph->Lifetimes().end(),
        [](const auto &lifetime) {
            return lifetime.resource_name == "SceneTLAS" &&
                   std::holds_alternative<kpengine::render::GraphAccelerationStructureHandle>(
                       lifetime.handle);
        }));
}

TEST(RenderGraphCompatibilityTest, RayTracingSceneColorKeepsDiagnosticCaptureIndependent)
{
    const auto result = CompileRenderFrameGraph(RenderFrameConditions{true, false, false, false, true});
    ASSERT_TRUE(result.Succeeded());

    const auto has_pass = [&result](const char *name) {
        return std::any_of(result.graph->Passes().begin(), result.graph->Passes().end(),
                           [name](const CompiledRenderGraph::Pass &pass) {
                               return pass.name == name;
                           });
    };
    EXPECT_TRUE(has_pass("GBufferPass"));
    EXPECT_TRUE(has_pass("CaptureViewPass"));
    EXPECT_TRUE(has_pass("RayTracingPathTracePass"));
    EXPECT_TRUE(has_pass("RayTracingToneMapPass"));
    EXPECT_FALSE(has_pass("DeferredLightingPass"));
    EXPECT_FALSE(has_pass("ToneMapPass"));
    EXPECT_TRUE(result.graph->Transients().empty());

    const auto path_trace = std::find_if(
        result.graph->Passes().begin(), result.graph->Passes().end(),
        [](const CompiledRenderGraph::Pass &pass) {
            return pass.name == "RayTracingPathTracePass";
        });
    ASSERT_NE(path_trace, result.graph->Passes().end());
    const auto history_read = std::find_if(
        path_trace->uses.begin(), path_trace->uses.end(),
        [](const RenderGraphResourceUse &use) {
            const auto *texture = std::get_if<GraphTextureHandle>(&use.handle);
            return texture &&
                   texture->resource == static_cast<std::size_t>(RenderPassResource::PathTraceHistory) &&
                   use.access == RenderGraphAccess::Read &&
                   use.usage == kpengine::render::RenderGraphUsage::StorageRead;
        });
    EXPECT_NE(history_read, path_trace->uses.end());
}

TEST(RenderGraphCompatibilityTest, AuthoredDeclarationIsCanonicalAndWellFormed)
{
    const std::vector<FixedRenderPassEntry> &entries = GetRenderFramePassEntries();
    ASSERT_EQ(entries.size(), static_cast<std::size_t>(FixedRenderPassId::Count));
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        // The compiled plan's key is the id, and pass identity indexes the
        // profile arrays and the backend GPU-profile slots.
        EXPECT_EQ(static_cast<std::size_t>(entries[index].id), index);
        EXPECT_FALSE(entries[index].name.empty());
    }
    const auto terminal = std::find_if(
        entries.begin(), entries.end(), [](const FixedRenderPassEntry &entry) {
            return entry.terminal;
        });
    ASSERT_NE(terminal, entries.end());
    EXPECT_EQ(terminal->id, FixedRenderPassId::EditorComposite);
}

TEST(RenderGraphCompatibilityTest, ScheduledAccelerationBuildsUseSSAAndDeclaredHazards)
{
    const auto result = CompileRenderFrameGraph(RenderFrameConditions{false, false, true, true});
    ASSERT_TRUE(result.Succeeded());

    const auto blas = std::find_if(
        result.graph->Passes().begin(), result.graph->Passes().end(),
        [](const CompiledRenderGraph::Pass &pass) {
            return pass.name == "RayTracingBlasBuildPass";
        });
    const auto tlas = std::find_if(
        result.graph->Passes().begin(), result.graph->Passes().end(),
        [](const CompiledRenderGraph::Pass &pass) {
            return pass.name == "RayTracingTlasBuildPass";
        });
    ASSERT_NE(blas, result.graph->Passes().end());
    ASSERT_NE(tlas, result.graph->Passes().end());
    ASSERT_LT(blas->declaration_index, tlas->declaration_index);
    ASSERT_FALSE(result.graph->Passes().empty());
    EXPECT_EQ(result.graph->Passes().back().name, "EditorCompositePass");
    EXPECT_TRUE(result.graph->Passes().back().terminal);

    const auto blas_write = std::find_if(
        blas->uses.begin(), blas->uses.end(), [](const RenderGraphResourceUse &use) {
            return use.access == RenderGraphAccess::Write &&
                   use.usage == kpengine::render::RenderGraphUsage::AccelerationStructureBuildOutput;
        });
    const auto tlas_write = std::find_if(
        tlas->uses.begin(), tlas->uses.end(), [](const RenderGraphResourceUse &use) {
            return use.access == RenderGraphAccess::Write &&
                   use.usage == kpengine::render::RenderGraphUsage::AccelerationStructureBuildOutput;
        });
    ASSERT_NE(blas_write, blas->uses.end());
    ASSERT_NE(tlas_write, tlas->uses.end());
    const auto *blas_handle =
        std::get_if<kpengine::render::GraphAccelerationStructureHandle>(&blas_write->handle);
    const auto *tlas_handle =
        std::get_if<kpengine::render::GraphAccelerationStructureHandle>(&tlas_write->handle);
    ASSERT_NE(blas_handle, nullptr);
    ASSERT_NE(tlas_handle, nullptr);
    EXPECT_EQ(blas_handle->version, 1U);
    EXPECT_EQ(tlas_handle->version, 1U);

    const auto tlas_input = std::find_if(
        tlas->uses.begin(), tlas->uses.end(), [](const RenderGraphResourceUse &use) {
            return use.access == RenderGraphAccess::Read &&
                   use.usage == kpengine::render::RenderGraphUsage::AccelerationStructureBuildInput;
        });
    ASSERT_NE(tlas_input, tlas->uses.end());
    const auto *tlas_input_handle =
        std::get_if<kpengine::render::GraphAccelerationStructureHandle>(&tlas_input->handle);
    ASSERT_NE(tlas_input_handle, nullptr);
    EXPECT_EQ(tlas_input_handle->version, 1U);
}

TEST(RenderGraphCompatibilityTest, AuthoredDeclarationCompilesIndependentConsumerOutputs)
{
    const std::array<RenderFrameConditions, 4> condition_sets{{
        RenderFrameConditions{false},
        RenderFrameConditions{true},
        RenderFrameConditions{false, false, false, false, false, true},
        RenderFrameConditions{true, false, false, false, false, true},
    }};
    for (const RenderFrameConditions conditions : condition_sets)
    {
        const auto result = CompileRenderFrameGraph(conditions);
        ASSERT_TRUE(result.Succeeded());
        const std::vector<std::string> expected = ExpectedPlannedPasses(conditions);
        const std::vector<FixedRenderPassEntry> &entries = GetRenderFramePassEntries();
        ASSERT_EQ(result.graph->Passes().size(), expected.size());

        std::array<uint32_t, kResourceCount> current_versions{};
        for (std::size_t pass_index = 0; pass_index < result.graph->Passes().size(); ++pass_index)
        {
            const CompiledRenderGraph::Pass &pass = result.graph->Passes()[pass_index];
            EXPECT_EQ(pass.name, expected[pass_index]);
            ASSERT_TRUE(pass.user_key.has_value());
            const std::size_t entry_index = static_cast<std::size_t>(*pass.user_key);
            ASSERT_LT(entry_index, entries.size());
            const FixedRenderPassEntry &entry = entries[entry_index];
            EXPECT_EQ(pass.name, entry.name);
            // The terminal carries each active consumer's sampled read so the
            // conversion outputs leave attachment layout before consumption.
            const std::size_t host_read_count =
                entry.owner == RenderPassExecutionOwner::External
                    ? static_cast<std::size_t>(conditions.diagnostic_capture) +
                          static_cast<std::size_t>(conditions.debug_view)
                    : 0U;
            const std::size_t active_resource_count = static_cast<std::size_t>(
                std::count_if(entry.resources.begin(), entry.resources.end(),
                              [&](const auto &use) {
                                  return IsResourceUseEnabled(use, conditions);
                              }));
            ASSERT_EQ(pass.uses.size(), active_resource_count + host_read_count);
            std::size_t compiled_use_index = 0;
            for (const RenderPassResourceUse &resource_use : entry.resources)
            {
                if (!IsResourceUseEnabled(resource_use, conditions))
                    continue;
                const auto *texture = std::get_if<GraphTextureHandle>(
                    &pass.uses[compiled_use_index].handle);
                ASSERT_NE(texture, nullptr);
                const std::size_t resource_index =
                    static_cast<std::size_t>(resource_use.resource);
                EXPECT_EQ(texture->resource, resource_index);
                const RenderGraphAccess expected_access =
                    resource_use.access == RenderPassAccess::Read
                        ? RenderGraphAccess::Read
                        : RenderGraphAccess::Write;
                EXPECT_EQ(pass.uses[compiled_use_index].access, expected_access);
                if (expected_access == RenderGraphAccess::Write)
                {
                    EXPECT_EQ(texture->version, current_versions[resource_index] + 1U);
                    current_versions[resource_index] = texture->version;
                }
                else
                {
                    EXPECT_EQ(texture->version, current_versions[resource_index]);
                }
                ++compiled_use_index;
            }
        }

        // The Editor terminal is compiled whatever the conditions, because
        // whether it runs is not known when the frame declares.
        ASSERT_FALSE(result.graph->Passes().empty());
        const CompiledRenderGraph::Pass &terminal = result.graph->Passes().back();
        EXPECT_EQ(terminal.name, "EditorCompositePass");
        EXPECT_EQ(terminal.owner, RenderGraphPassOwner::External);
        EXPECT_TRUE(terminal.terminal);

        // Each consumer has a separate conversion pass and output.
        const auto capture_pass = std::find_if(
            result.graph->Passes().begin(), result.graph->Passes().end(),
            [](const CompiledRenderGraph::Pass &pass) {
                return pass.user_key ==
                       static_cast<uint64_t>(FixedRenderPassId::CaptureView);
            });
        EXPECT_EQ(capture_pass != result.graph->Passes().end(), conditions.diagnostic_capture);
        const auto debug_pass = std::find_if(
            result.graph->Passes().begin(), result.graph->Passes().end(),
            [](const CompiledRenderGraph::Pass &pass) {
                return pass.user_key == static_cast<uint64_t>(FixedRenderPassId::DebugView);
            });
        EXPECT_EQ(debug_pass != result.graph->Passes().end(), conditions.debug_view);
    }
}

TEST(RenderGraphCompatibilityTest, TypedPlanCarriesExactGraphResourceRolesAndVersions)
{
    const RenderFrameConditions conditions{true, true, true, true, true, true};
    const CompiledRenderFramePlan plan = CompileRenderFramePlan(conditions);
    ASSERT_TRUE(plan.Succeeded());
    ASSERT_TRUE(plan.compilation.graph.has_value());
    const uint64_t graph_id = plan.compilation.graph->GraphId();

    const auto has_role = [&plan](RenderFrameResourceRole role) {
        return std::any_of(plan.resources.begin(), plan.resources.end(),
                           [role](const RenderFrameResourceImport &entry) {
                               return entry.role == role;
                           });
    };
    for (const RenderFrameResourceRole role : {
             RenderFrameResourceRole::SceneColor,
             RenderFrameResourceRole::SceneHdr,
             RenderFrameResourceRole::GBuffer,
             RenderFrameResourceRole::CaptureOutput,
             RenderFrameResourceRole::DebugViewOutput,
             RenderFrameResourceRole::PathTraceHistory,
             RenderFrameResourceRole::PathTraceGuide,
             RenderFrameResourceRole::SceneGeometry,
             RenderFrameResourceRole::SceneScratch,
             RenderFrameResourceRole::SceneBlas,
             RenderFrameResourceRole::SceneTlas})
    {
        EXPECT_TRUE(has_role(role)) << static_cast<int>(role);
    }

    for (const RenderFrameResourceImport &resource : plan.resources)
    {
        std::visit(
            [graph_id](const auto &handle) {
                EXPECT_TRUE(handle.IsValid());
                EXPECT_EQ(handle.graph_id, graph_id);
            },
            resource.handle);
    }

    const auto has_tlas_version = [&plan](uint32_t version) {
        return std::any_of(plan.resources.begin(), plan.resources.end(),
                           [version](const RenderFrameResourceImport &entry) {
                               const auto *handle = std::get_if<GraphAccelerationStructureHandle>(
                                   &entry.handle);
                               return entry.role == RenderFrameResourceRole::SceneTlas &&
                                      handle != nullptr && handle->version == version;
                           });
    };
    EXPECT_FALSE(has_tlas_version(0));
    EXPECT_TRUE(has_tlas_version(1));
}

TEST(RenderGraphCompatibilityTest, LegacyCompileAdapterReturnsSameTypedPlanGraph)
{
    const RenderFrameConditions conditions{false, true, true, true, false, false};
    const CompiledRenderFramePlan typed = CompileRenderFramePlan(conditions);
    const RenderGraphCompileResult compatibility = CompileRenderFrameGraph(conditions);
    ASSERT_TRUE(typed.Succeeded());
    ASSERT_TRUE(compatibility.Succeeded());
    ASSERT_TRUE(typed.compilation.graph.has_value());
    ASSERT_TRUE(compatibility.graph.has_value());

    const auto names = [](const CompiledRenderGraph &graph) {
        std::vector<std::string> result;
        for (const CompiledRenderGraph::Pass &pass : graph.Passes())
        {
            result.push_back(pass.name);
        }
        return result;
    };
    EXPECT_EQ(names(*typed.compilation.graph), names(*compatibility.graph));
    EXPECT_EQ(typed.compilation.graph->Transients().size(),
              compatibility.graph->Transients().size());
}

TEST(RenderGraphCompatibilityTest, TypedImportMetadataCoversAllSixtyFourVariants)
{
    for (uint32_t bits = 0; bits < 64; ++bits)
    {
        const RenderFrameConditions conditions{
            (bits & 1U) != 0, (bits & 2U) != 0, (bits & 4U) != 0,
            (bits & 8U) != 0, (bits & 16U) != 0, (bits & 32U) != 0};
        const CompiledRenderFramePlan plan = CompileRenderFramePlan(conditions);
        ASSERT_TRUE(plan.Succeeded()) << "condition bits=" << bits;
        ASSERT_TRUE(plan.compilation.graph.has_value());
        const CompiledRenderGraph &graph = *plan.compilation.graph;

        std::vector<RenderFrameGraphResourceHandle> declared_handles;
        for (const CompiledRenderGraph::Pass &pass : graph.Passes())
        {
            for (const RenderGraphResourceUse &use : pass.uses)
            {
                if (std::find(declared_handles.begin(), declared_handles.end(), use.handle) ==
                    declared_handles.end())
                {
                    declared_handles.push_back(use.handle);
                }
            }
        }
        ASSERT_EQ(plan.resources.size(), declared_handles.size())
            << "condition bits=" << bits;

        for (const auto &handle : declared_handles)
        {
            const auto binding = std::find_if(
                plan.resources.begin(), plan.resources.end(),
                [&handle](const RenderFrameResourceImport &entry) {
                    return entry.handle == handle;
                });
            ASSERT_NE(binding, plan.resources.end()) << "condition bits=" << bits;
            EXPECT_NE(binding->role, RenderFrameResourceRole::Count)
                << "condition bits=" << bits << ", missing authored role";
        }

        const auto role_is_present = [&plan](RenderFrameResourceRole role) {
            return std::any_of(plan.resources.begin(), plan.resources.end(),
                               [role](const RenderFrameResourceImport &entry) {
                                   return entry.role == role;
                               });
        };
        EXPECT_EQ(role_is_present(RenderFrameResourceRole::CaptureOutput),
                  conditions.diagnostic_capture);
        EXPECT_EQ(role_is_present(RenderFrameResourceRole::DebugViewOutput),
                  conditions.debug_view);
        EXPECT_EQ(role_is_present(RenderFrameResourceRole::SceneGeometry),
                  conditions.ray_tracing_blas_build || conditions.ray_tracing_path_trace);
        EXPECT_EQ(role_is_present(RenderFrameResourceRole::SceneInstances),
                  conditions.ray_tracing_tlas_build);
    }
}

TEST(RenderGraphCompatibilityTest, TaaRolesRemainCorrectWhenAoResourcesArePruned)
{
    using namespace kpengine::render;
    RenderFrameConditions conditions{};
    conditions.screen_space_ao = false;
    conditions.temporal_aa = true;
    const auto plan = CompileRenderFramePlan(conditions);
    ASSERT_TRUE(plan.compilation.graph.has_value());
    const auto &passes = plan.compilation.graph->Passes();
    const auto taa = std::find_if(passes.begin(), passes.end(), [](const auto &pass) {
        return pass.name == "TemporalAaResolvePass";
    });
    ASSERT_NE(taa, passes.end());
    std::vector<RenderFrameResourceRole> roles;
    for (const auto &use : taa->uses)
    {
        const auto physical = std::find_if(plan.resources.begin(), plan.resources.end(),
            [&](const auto &resource) { return resource.handle == use.handle; });
        ASSERT_NE(physical, plan.resources.end());
        roles.push_back(physical->role);
    }
    EXPECT_NE(std::find(roles.begin(), roles.end(), RenderFrameResourceRole::SceneHdr), roles.end());
    EXPECT_NE(std::find(roles.begin(), roles.end(), RenderFrameResourceRole::GBuffer), roles.end());
    EXPECT_NE(std::find(roles.begin(), roles.end(), RenderFrameResourceRole::TemporalAaHistoryRead), roles.end());
    EXPECT_NE(std::find(roles.begin(), roles.end(), RenderFrameResourceRole::TemporalAaOutput), roles.end());
    EXPECT_EQ(std::find(roles.begin(), roles.end(), RenderFrameResourceRole::ScreenSpaceAoRaw), roles.end());
    EXPECT_EQ(std::find(roles.begin(), roles.end(), RenderFrameResourceRole::ScreenSpaceAoFiltered), roles.end());
}

TEST(RenderGraphCompatibilityTest, FogFeedsTheFirstHdrConsumerAndIsPrunedForPathTracing)
{
    using namespace kpengine::render;
    RenderFrameConditions conditions{};
    conditions.height_fog = true;
    for (bool taa : {false, true})
    {
        conditions.temporal_aa = taa;
        const auto plan = CompileRenderFramePlan(conditions);
        ASSERT_TRUE(plan.Succeeded());
        const auto &graph = *plan.compilation.graph;
        const auto &passes = graph.Passes();
        const auto find = [&](FixedRenderPassId id) {
            return std::find_if(passes.begin(), passes.end(), [id](const auto &pass) {
                return pass.user_key == static_cast<uint64_t>(id);
            });
        };
        const auto fog = find(FixedRenderPassId::HeightFog);
        const auto consumer = find(taa ? FixedRenderPassId::TemporalAaResolve : FixedRenderPassId::ToneMap);
        ASSERT_NE(fog, passes.end());
        ASSERT_NE(consumer, passes.end());
        EXPECT_LT(find(FixedRenderPassId::DeferredLighting), fog);
        EXPECT_LT(fog, consumer);
        const auto reads_fog = [&](const auto &use) {
            return use.access == RenderGraphAccess::Read &&
                std::any_of(plan.resources.begin(), plan.resources.end(), [&](const auto &resource) {
                    return resource.handle == use.handle && resource.role == RenderFrameResourceRole::FoggedHdr;
                });
        };
        EXPECT_TRUE(std::any_of(consumer->uses.begin(), consumer->uses.end(), reads_fog));
    }
    conditions.ray_tracing_path_trace = true;
    const auto pt = CompileRenderFramePlan(conditions);
    ASSERT_TRUE(pt.Succeeded());
    EXPECT_FALSE(std::any_of(pt.resources.begin(), pt.resources.end(), [](const auto &resource) {
        return resource.role == RenderFrameResourceRole::FoggedHdr;
    }));
}
