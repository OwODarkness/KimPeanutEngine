#include "terrain_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>

#include <imgui.h>

#include "editor/log/editor_log_component.h"
#include "editor/profile/editor_builtin_metrics.h"
#include "editor/profile/editor_metric.h"
#include "editor/profile/editor_profile_bar.h"
#include "editor/settings/editor_settings.h"
#include "editor/ui/component/editor_gpu_profiler_component.h"
#include "editor/ui/component/editor_tool_row_component.h"
#include "editor/ui/component/editor_window_component.h"
#include "editor/ui/editor_ui.h"
#include "render/render_system.h"
#include "runtime/engine.h"
#include "runtime/runtime_global_context.h"
#include "runtime/platform/memory_stats_sampler.h"
#include "runtime/window/window_system.h"
#include "product/terrain_core.h"

namespace kpengine::terrain
{
    namespace
    {
        class TerrainEditorDockPanel final : public editor::EditorWindowComponent
        {
        public:
            TerrainEditorDockPanel(std::string title, std::function<void()> render_content)
                : EditorWindowComponent(title), render_content_(std::move(render_content))
            {
            }

            void RenderContent() override
            {
                if (render_content_)
                {
                    render_content_();
                }
            }

        private:
            std::function<void()> render_content_;
        };

        ImVec2 FitView(const graphics::RenderTargetView &view, const ImVec2 &available)
        {
            if (!view.IsValid() || available.x <= 0.0f || available.y <= 0.0f)
            {
                return available;
            }
            const float aspect = static_cast<float>(view.width) /
                                 static_cast<float>(view.height);
            const float available_aspect = available.x / available.y;
            return available_aspect > aspect
                       ? ImVec2(available.y * aspect, available.y)
                       : ImVec2(available.x, available.x / aspect);
        }

        std::shared_ptr<const ScalarField2D> AsScalarView(const TerrainValue &value)
        {
            if (!value) return {};
            if (const auto *scalar = value->AsScalarField())
                return std::shared_ptr<const ScalarField2D>(value, scalar);
            std::string diagnostic;
            return ScalarField2D::Create(value->Domain(), value->Samples(),
                value->Samples().size(), diagnostic);
        }
    }

    TerrainEditor::TerrainEditor() = default;

    TerrainEditor::~TerrainEditor()
    {
        Shutdown();
    }

    bool TerrainEditor::Initialize(
        runtime::Engine &engine,
        std::shared_ptr<const ScalarField2D> heightfield,
        std::string &diagnostic,
        std::function<void(std::uint64_t, std::uint32_t, std::uint32_t, float, float,
                           float, float, std::uint32_t)> regenerate,
        std::function<void()> cancel,
        std::function<void(int)> execution_control,
        std::function<void(float, float, float)> camera_control,
        std::function<void()> bake)
    {
        diagnostic.clear();
        runtime::RuntimeContext &context = runtime::global_runtime_context;
        if (!heightfield || context.window_system_ == nullptr ||
            context.render_system_ == nullptr || context.log_system_ == nullptr)
        {
            diagnostic = "Terrain editor presentation services are unavailable";
            return false;
        }

        heightfield_ = std::move(heightfield);
        regenerate_ = std::move(regenerate);
        cancel_ = std::move(cancel);
        execution_control_ = std::move(execution_control);
        camera_control_ = std::move(camera_control);
        bake_ = std::move(bake);
        editor::EditorUIInitInfo init_info{};
        init_info.window = context.window_system_->GetNativeHandle();
        init_info.editor_presentation_bridge =
            context.render_system_->GetEditorPresentationBridge();
        init_info.log_system = context.log_system_.get();
        init_info.engine = &engine;
        init_info.render_system = context.render_system_.get();
        init_info.window_system = context.window_system_.get();

        try
        {
            ui_ = std::make_unique<editor::EditorUI>();
            ui_->InitializeViewer(init_info, [this] { RenderPanels(); });
            log_panel_ = std::make_unique<editor::EditorLogComponent>(
                context.log_system_.get(), editor::DefaultLogColors());
            performance_panel_ =
                std::make_unique<editor::EditorGpuProfilerComponent>(
                    &engine, context.render_system_.get(), ui_.get());

            std::vector<std::unique_ptr<editor::EditorMetric>> profile_metrics;
            profile_metrics.push_back(std::make_unique<editor::EditorFPSMetric>(
                [&engine] { return engine.GetFPS(); }));
            profile_metrics.push_back(std::make_unique<editor::EditorFrameTimeMetric>(
                [&engine] {
                    const int fps = engine.GetFPS();
                    return fps > 0 ? 1000.0f / static_cast<float>(fps) : 0.0f;
                }));
            profile_metrics.push_back(std::make_unique<editor::EditorFuncMetric>(
                "CPU", [render_system = context.render_system_.get()] {
                    char value[32]{};
                    std::snprintf(value, sizeof(value), "%.2f ms",
                        render_system->GetMetrics().profile.cpu_total_ms);
                    return std::string{value};
                }));
            profile_metrics.push_back(std::make_unique<editor::EditorFuncMetric>(
                "GPU", [render_system = context.render_system_.get()] {
                    const auto profile = render_system->GetMetrics().profile;
                    double total = 0.0;
                    bool measured = false;
                    for (const auto &pass : profile.passes)
                    {
                        if (pass.gpu_time_ms.has_value())
                        {
                            total += *pass.gpu_time_ms;
                            measured = true;
                        }
                    }
                    if (!measured) return std::string{"N/A"};
                    char value[32]{};
                    std::snprintf(value, sizeof(value), "%.2f ms", total);
                    return std::string{value};
                }));
            profile_metrics.push_back(std::make_unique<editor::EditorFuncMetric>(
                "API", [render_system = context.render_system_.get()] {
                    switch (render_system->GetMetrics().profile.graphics_api)
                    {
                    case GraphicsAPIType::GRAPHICS_API_OPENGL: return std::string{"OpenGL"};
                    case GraphicsAPIType::GRAPHICS_API_VULKAN: return std::string{"Vulkan"};
                    default: return std::string{"Unknown"};
                    }
                }));
            if (context.memory_sampler_)
            {
                profile_metrics.push_back(std::make_unique<editor::EditorMemoryMetric>(
                    [sampler = context.memory_sampler_.get()] {
                        const MemoryStats stats = sampler->Sample();
                        return editor::EditorMemoryMetric::Stats{
                            stats.process_mb, stats.system_available_mb};
                    }));
            }
            profile_bar_ = std::make_unique<editor::EditorProfileBarComponent>(
                std::move(profile_metrics));

            layout_.ResetToCompactViewerDefault();
            dock_model_.Clear();
            dock_host_ = std::make_unique<editor::EditorToolRowComponent>(
                dock_model_, editor::EditorWindowConfig{});
            dock_host_->SetLayoutModel(&layout_);
            dock_host_->AddPanel(
                "terrain_view", "Terrain View",
                std::make_unique<TerrainEditorDockPanel>(
                    "Terrain View", [this] { RenderTerrainView(); }),
                true, editor::EditorLayoutSlot::Viewport);
            dock_host_->AddPanel(
                "heightmap_debug", "Heightmap Debug",
                std::make_unique<TerrainEditorDockPanel>(
                    "Heightmap Debug", [this] {
                        std::lock_guard lock(snapshot_mutex_);
                        heightmap_view_.RenderContent(heightfield_.get(),
                                                      pre_erosion_heightfield_.get());
                    }),
                true, editor::EditorLayoutSlot::DebugViewer);
            dock_host_->AddPanel(
                "terrain_performance", "Performance Profiler",
                std::make_unique<TerrainEditorDockPanel>(
                    "Performance Profiler",
                    [this] { performance_panel_->RenderContent(); }),
                true, editor::EditorLayoutSlot::GpuProfiler);
            dock_host_->AddPanel(
                "terrain_log", "Log",
                std::make_unique<TerrainEditorDockPanel>(
                    "Log", [this] { log_panel_->RenderContent(); }),
                true, editor::EditorLayoutSlot::ToolRow);
            dock_host_->AddPanel(
                "terrain_controls", "Terrain Controls",
                std::make_unique<TerrainEditorDockPanel>(
                    "Terrain Controls", [this] { RenderControlPanel(); }),
                true, editor::EditorLayoutSlot::CameraSettings);
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("Terrain editor UI initialization failed: ") +
                         error.what();
            Shutdown();
            return false;
        }
        return true;
    }

    void TerrainEditor::SetEvaluationSnapshot(
        std::shared_ptr<const ScalarField2D> heightfield,
        const EvaluationResult &result, const TerrainRecipe &recipe, std::string status)
    {
        std::lock_guard lock(snapshot_mutex_);
        if (heightfield) heightfield_ = std::move(heightfield);
        seed_ = recipe.seed;
        const auto landform = std::find_if(recipe.nodes.begin(), recipe.nodes.end(),
            [](const RecipeNode &node) {
                return node.operator_id == "terrain.heightfield.perlin_fbm";
            });
        if (landform != recipe.nodes.end())
        {
            lattice_size_ = landform->parameters.value("lattice_size", lattice_size_);
            octaves_ = landform->parameters.value("octaves", octaves_);
            persistence_ = landform->parameters.value("persistence", persistence_);
            lacunarity_ = landform->parameters.value("lacunarity", lacunarity_);
        }
        const auto erosion = std::find_if(recipe.nodes.begin(), recipe.nodes.end(),
            [](const RecipeNode &node) {
                return node.operator_id == "terrain.erosion.thermal_flux";
            });
        pre_erosion_heightfield_.reset();
        thermal_erosion_enabled_ = erosion != recipe.nodes.end();
        if (erosion != recipe.nodes.end())
        {
            talus_angle_degrees_ = erosion->parameters.value(
                "talus_angle_degrees", talus_angle_degrees_);
            thermal_rate_ = erosion->parameters.value("thermal_rate", thermal_rate_);
            thermal_iterations_ = erosion->parameters.value("iterations", thermal_iterations_);
            const auto eroded_node = result.nodes.find(erosion->id);
            if (eroded_node != result.nodes.end())
            {
                const auto eroded_field = eroded_node->second.outputs.find("height");
                if (eroded_field != eroded_node->second.outputs.end())
                    heightfield_ = AsScalarView(eroded_field->second);
            }
            const auto source = erosion->inputs.find("source");
            if (source != erosion->inputs.end())
            {
                const auto source_node = result.nodes.find(source->second.node);
                if (source_node != result.nodes.end())
                {
                    const auto source_field = source_node->second.outputs.find(source->second.port);
                    if (source_field != source_node->second.outputs.end())
                        pre_erosion_heightfield_ = AsScalarView(source_field->second);
                }
            }
            if (eroded_node == result.nodes.end() ||
                !eroded_node->second.outputs.contains("height"))
                pre_erosion_heightfield_.reset();
        }
        generation_status_ = std::move(status);
        generation_diagnostic_ = result.diagnostic;
        node_diagnostics_ = result.nodes;
    }

    bool TerrainEditor::Render(std::string &diagnostic)
    {
        diagnostic.clear();
        if (!ui_ || !ui_->Render())
        {
            diagnostic = "Terrain editor ImGui presentation failed";
            return false;
        }
        return true;
    }

    void TerrainEditor::Shutdown() noexcept
    {
        if (ui_)
        {
            ui_->Close();
            ui_.reset();
        }
        dock_host_.reset();
        performance_panel_.reset();
        profile_bar_.reset();
        log_panel_.reset();
        heightfield_.reset();
        pre_erosion_heightfield_.reset();
        regenerate_ = {};
        cancel_ = {};
        execution_control_ = {};
        camera_control_ = {};
        bake_ = {};
    }

    void TerrainEditor::RenderPanels()
    {
        ApplyLayout();
        if (dock_host_)
        {
            dock_host_->Render();
        }
        splitter_handles_.Render(layout_);
        if (profile_bar_) profile_bar_->Render();
    }

    void TerrainEditor::ApplyLayout()
    {
        const ImGuiViewport *const viewport = ImGui::GetMainViewport();
        layout_.SetFixedExtentPixels(
            editor::EditorSplitterId::StatusBar,
            editor::EditorProfileBarComponent::MeasurePreferredHeightPx());
        layout_.Resolve({viewport->WorkPos.x, viewport->WorkPos.y,
                         viewport->WorkSize.x, viewport->WorkSize.y});
        if (profile_bar_)
            profile_bar_->ApplyLayout(layout_.RectOf(editor::EditorLayoutSlot::ProfileBar));
    }

    void TerrainEditor::RenderTerrainView()
    {
        render::RenderSystem *const render_system =
            runtime::global_runtime_context.render_system_.get();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (render_system != nullptr && available.x > 0.0f && available.y > 0.0f)
        {
            render_system->RequestSceneRenderTargetExtent(
                static_cast<std::uint32_t>(available.x),
                static_cast<std::uint32_t>(available.y));
            const graphics::RenderTargetView view = render_system->GetSceneRenderTargetView();
            const ImVec2 image_size = FitView(view, available);
            ImGui::SetCursorPos(ImVec2(
                ImGui::GetCursorPosX() + std::max(0.0f, (available.x - image_size.x) * 0.5f),
                ImGui::GetCursorPosY() + std::max(0.0f, (available.y - image_size.y) * 0.5f)));
            ui_->DrawRenderTarget(view, image_size);
            if (ImGui::IsItemHovered())
            {
                bool changed = false;
                const float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f)
                {
                    camera_distance_ = std::clamp(camera_distance_ *
                        std::pow(0.88f, wheel), 20.0f, 4000.0f);
                    changed = true;
                }
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
                {
                    const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                    camera_yaw_degrees_ += delta.x * 0.25f;
                    camera_pitch_degrees_ = std::clamp(
                        camera_pitch_degrees_ - delta.y * 0.25f, -85.0f, -5.0f);
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
                    changed = true;
                }
                if (changed && camera_control_)
                    camera_control_(camera_yaw_degrees_, camera_pitch_degrees_, camera_distance_);
            }
        }
    }

    void TerrainEditor::RenderControlPanel()
    {
        std::lock_guard lock(snapshot_mutex_);
        ImGui::Text("Generation: %s", generation_status_.c_str());
        if (!generation_diagnostic_.empty())
            ImGui::TextWrapped("%s", generation_diagnostic_.c_str());
        ImGui::InputScalar("Seed", ImGuiDataType_U64, &seed_);
        ImGui::TextDisabled("Fine detail noise");
        ImGui::InputScalar("Lattice size", ImGuiDataType_U32, &lattice_size_);
        int detail_octaves = static_cast<int>(octaves_);
        if (ImGui::SliderInt("Detail octaves", &detail_octaves, 1, 16))
            octaves_ = static_cast<std::uint32_t>(detail_octaves);
        ImGui::InputFloat("Detail persistence", &persistence_, 0.05f, 0.1f, "%.2f");
        ImGui::InputFloat("Detail lacunarity", &lacunarity_, 0.1f, 0.5f, "%.2f");
        if (thermal_erosion_enabled_)
        {
            ImGui::SeparatorText("Thermal Erosion");
            ImGui::SliderFloat("Talus Angle", &talus_angle_degrees_, 5.0f, 60.0f, "%.1f deg");
            ImGui::SliderFloat("Thermal Rate", &thermal_rate_, 0.01f, 1.0f, "%.2f");
            int iterations = static_cast<int>(thermal_iterations_);
            if (ImGui::SliderInt("Iteration Count", &iterations, 1, 100))
                thermal_iterations_ = static_cast<std::uint32_t>(iterations);
        }
        else
        {
            ImGui::TextDisabled("Thermal erosion disabled for this recipe");
        }
        if (ImGui::Button("Regenerate") && regenerate_)
            regenerate_(seed_, lattice_size_, octaves_, persistence_, lacunarity_,
                        talus_angle_degrees_, thermal_rate_, thermal_iterations_);
        ImGui::SameLine();
        if (ImGui::Button("Cancel") && cancel_) cancel_();
        if (ImGui::Button("Pause at node") && execution_control_) execution_control_(0);
        ImGui::SameLine();
        if (ImGui::Button("Step node") && execution_control_) execution_control_(1);
        ImGui::SameLine();
        if (ImGui::Button("Resume") && execution_control_) execution_control_(2);
        ImGui::SameLine();
        if (ImGui::Button("Bake Native Asset") && bake_) bake_();
        if (ImGui::CollapsingHeader("Node diagnostics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const auto &[node_id, node] : node_diagnostics_)
            {
                if (ImGui::TreeNode(node_id.c_str()))
                {
                    for (const auto &[output_name, field] : node.outputs)
                    {
                        const std::string label = output_name + "##" + node_id;
                        if (ImGui::SmallButton(label.c_str()) && field)
                        {
                            auto scalar = AsScalarView(field);
                            if (scalar)
                            {
                                heightfield_ = std::move(scalar);
                                selected_field_name_ = node_id + "." + output_name;
                            }
                        }
                    }
                    for (const auto &[name, value] : node.scalar_metadata)
                        ImGui::Text("%s: %.9g", name.c_str(), value);
                    ImGui::TreePop();
                }
                ImGui::Text("%s: %.3f ms | %zu bytes | [%.3f, %.3f] | %s | %016llx",
                    node_id.c_str(), node.evaluation_time_ms, node.output_bytes,
                    static_cast<double>(node.minimum_value),
                    static_cast<double>(node.maximum_value),
                    node.all_values_finite ? "finite" : "non-finite",
                    static_cast<unsigned long long>(node.content_hash));
            }
            ImGui::Text("Heightmap selection: %s", selected_field_name_.c_str());
        }
    }
}
