#include "terrain_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "editor/ui/api/editor_ui/ui_backend.h"
#include "editor/ui/api/editor_ui/ui_theme.h"
#include "editor/ui/host/editor_ui_host.h"
#include "product/terrain_core.h"
#include "render/render_system.h"
#include "runtime/engine.h"
#include "runtime/platform/memory_stats_sampler.h"
#include "runtime/runtime_global_context.h"
#include "runtime/window/window_system.h"
#include "runtime/core/log/editor/log_editor_view.h"

namespace kpengine::terrain
{
    namespace
    {
        const editor::ui::UiImageId kTerrainImageId =
            editor::ui::StableUiImageId("terrain.scene.output");

        std::shared_ptr<const ScalarField2D> AsScalarView(const TerrainValue &value)
        {
            if (!value) return {};
            if (const auto *scalar = value->AsScalarField())
                return std::shared_ptr<const ScalarField2D>(value, scalar);
            std::string diagnostic;
            return ScalarField2D::Create(value->Domain(), value->Samples(),
                value->Samples().size(), diagnostic);
        }

        std::string Number(const double value, const char *format = "%.3g")
        {
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), format, value);
            return buffer;
        }

        void Field(editor::ui::UiContext &ui, const std::string_view label,
                   const std::string_view value)
        {
            ui.Text(std::string(label) + "  " + std::string(value), editor::ui::UiTextRole::Muted);
        }

        editor::ui::UiTheme TerrainTheme()
        {
            editor::ui::UiTheme theme{};
            theme.surface = {0.065F, 0.067F, 0.060F, 1.0F};
            theme.panel = {0.095F, 0.100F, 0.087F, 1.0F};
            theme.raised = {0.145F, 0.148F, 0.126F, 1.0F};
            theme.border = {0.31F, 0.30F, 0.24F, 1.0F};
            theme.text = {0.90F, 0.90F, 0.82F, 1.0F};
            theme.muted = {0.65F, 0.67F, 0.57F, 1.0F};
            theme.accent = {0.68F, 0.76F, 0.43F, 1.0F};
            theme.accent_active = {0.93F, 0.68F, 0.30F, 1.0F};
            theme.selected = {0.25F, 0.25F, 0.15F, 1.0F};
            theme.selected_hovered = {0.34F, 0.34F, 0.20F, 1.0F};
            return theme;
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
        runtime::RuntimeContext &runtime_context = runtime::global_runtime_context;
        if (!heightfield || runtime_context.window_system_ == nullptr ||
            runtime_context.render_system_ == nullptr || runtime_context.log_system_ == nullptr)
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

        try
        {
            ui_ = std::make_unique<editor::EditorUiHost>();
            log_panel_ = std::make_unique<editor::LogEditorView>();
            ui_->Initialize({
                .window = runtime_context.window_system_->GetNativeHandle(),
                .presentation_bridge = runtime_context.render_system_->GetEditorPresentationBridge()});
        }
        catch (const std::exception &error)
        {
            diagnostic = std::string("Terrain editor UI initialization failed: ") + error.what();
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
        if (!ui_ || !ui_->RenderFrame(
                [this](editor::ui::UiContext &ui) { RenderWorkspace(ui); }))
        {
            diagnostic = "Terrain editor UI presentation failed";
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
        log_panel_.reset();
        heightfield_.reset();
        pre_erosion_heightfield_.reset();
        regenerate_ = {};
        cancel_ = {};
        execution_control_ = {};
        camera_control_ = {};
        bake_ = {};
    }

    void TerrainEditor::RenderWorkspace(editor::ui::UiContext &ui)
    {
        using namespace editor::ui;
        const UiViewportInfo &viewport = ui.Viewport();
        UiTheme theme = TerrainTheme();
        [[maybe_unused]] const auto theme_scope = ui.Theme(theme);
        const UiVec2 origin = viewport.logical_origin;
        const UiVec2 extent = viewport.logical_extent;

        {
            const auto window = ui.Window({.id = "terrain.view.window", .label = "Terrain View"},
                origin, {extent.x * 0.70F, extent.y * 0.75F});
            if (window.ContentVisible()) RenderTerrainView(ui);
        }
        {
            const UiVec2 position{origin.x + extent.x * 0.70F, origin.y};
            const auto window = ui.Window({.id = "terrain.controls.window", .label = "Terrain Controls"},
                position, {extent.x * 0.30F, extent.y * 0.43F});
            if (window.ContentVisible()) RenderControlPanel(ui);
        }
        {
            const UiVec2 position{origin.x + extent.x * 0.70F,
                                  origin.y + extent.y * 0.43F};
            const auto window = ui.Window({.id = "terrain.heightmap.window", .label = "Heightmap Debug"},
                position, {extent.x * 0.30F, extent.y * 0.32F});
            if (window.ContentVisible())
            {
                std::lock_guard lock(snapshot_mutex_);
                heightmap_view_.RenderContent(ui, heightfield_.get(),
                                              pre_erosion_heightfield_.get());
            }
        }
        {
            const UiVec2 position{origin.x, origin.y + extent.y * 0.75F};
            const auto window = ui.Window({.id = "terrain.log.window", .label = "Log"},
                position, {extent.x, extent.y * 0.25F});
            if (window.ContentVisible()) RenderLog(ui);
        }
    }

    void TerrainEditor::RenderTerrainView(editor::ui::UiContext &ui)
    {
        using namespace editor::ui;
        render::RenderSystem *const render_system =
            runtime::global_runtime_context.render_system_.get();
        if (render_system == nullptr)
        {
            ui.Text("Render system is unavailable.", UiTextRole::Warning);
            return;
        }
        UiVec2 available = ui.AvailableContent();
        available.x = std::max(available.x, 1.0F);
        available.y = std::max(available.y, 1.0F);
        render_system->RequestSceneRenderTargetExtent(
            static_cast<std::uint32_t>(available.x),
            static_cast<std::uint32_t>(available.y));
        const graphics::RenderTargetView view = render_system->GetSceneRenderTargetView();
        std::vector<UiCanvasPrimitive> primitives;
        UiCanvasPrimitive background{};
        background.kind = UiCanvasPrimitiveKind::FilledRect;
        background.first = {0.0F, 0.0F};
        background.second = {1.0F, 1.0F};
        background.color = {0.035F, 0.040F, 0.035F, 1.0F};
        primitives.push_back(std::move(background));
        UiVec2 image_min{};
        UiVec2 image_max{1.0F, 1.0F};
        if (view.IsValid())
        {
            ui_->RegisterRenderTargetImage(kTerrainImageId, view);
            const float aspect = static_cast<float>(view.width) / view.height;
            const float available_aspect = available.x / available.y;
            const UiVec2 image_size = available_aspect > aspect
                ? UiVec2{available.y * aspect, available.y}
                : UiVec2{available.x, available.x / aspect};
            image_min = {(available.x - image_size.x) * 0.5F / available.x,
                         (available.y - image_size.y) * 0.5F / available.y};
            image_max = {image_min.x + image_size.x / available.x,
                         image_min.y + image_size.y / available.y};
            UiCanvasPrimitive image{};
            image.kind = UiCanvasPrimitiveKind::Image;
            image.first = image_min;
            image.second = image_max;
            image.image = kTerrainImageId;
            primitives.push_back(std::move(image));
        }
        else
        {
            UiCanvasPrimitive message{};
            message.kind = UiCanvasPrimitiveKind::Text;
            message.first = {0.5F, 0.5F};
            message.color = {0.65F, 0.67F, 0.57F, 1.0F};
            message.text = "Terrain render target unavailable";
            message.text_anchor = {0.5F, 0.5F};
            primitives.push_back(std::move(message));
        }
        const UiCanvasResult canvas = ui.Canvas(
            {.id = "terrain.view.canvas", .label = "Terrain viewport"},
            available, std::move(primitives));
        if (view.IsValid())
        {
            const bool changed = canvas.hovered && canvas.wheel_delta != 0.0F;
            if (changed)
                camera_distance_ = std::clamp(camera_distance_ *
                    std::pow(0.88F, canvas.wheel_delta), 20.0F, 4000.0F);
            if (canvas.secondary_dragging)
            {
                camera_yaw_degrees_ += canvas.pointer_delta.x * 0.25F;
                camera_pitch_degrees_ = std::clamp(
                    camera_pitch_degrees_ - canvas.pointer_delta.y * 0.25F, -85.0F, -5.0F);
            }
            if ((changed || canvas.secondary_dragging) && camera_control_)
                camera_control_(camera_yaw_degrees_, camera_pitch_degrees_, camera_distance_);
        }
    }

    void TerrainEditor::RenderControlPanel(editor::ui::UiContext &ui)
    {
        using namespace editor::ui;
        const auto tabs = ui.TabBar({.id = "terrain.controls.tabs", .label = "Terrain controls"});
        if (!tabs.Visible()) return;
        {
            const auto tab = ui.TabItem({.id = "terrain.controls.generation", .label = "Generation"});
            if (tab.ContentVisible())
            {
                std::lock_guard lock(snapshot_mutex_);
                ui.Text("Generation: " + generation_status_, UiTextRole::Accent);
                if (!generation_diagnostic_.empty()) ui.WrappedText(generation_diagnostic_);
                auto seed = ui.NumericInput<std::uint64_t>(
                    {.id = "terrain.generation.seed", .label = "Seed"}, seed_,
                    0ULL, std::numeric_limits<std::uint64_t>::max(), 1ULL);
                auto lattice = ui.NumericInput<std::uint64_t>(
                    {.id = "terrain.generation.lattice", .label = "Lattice size"}, lattice_size_,
                    1ULL, 1024ULL, 1ULL);
                auto octaves = ui.Slider<std::int64_t>(
                    {.id = "terrain.generation.octaves", .label = "Detail octaves"},
                    octaves_, 1, 16);
                auto persistence = ui.NumericInput<double>(
                    {.id = "terrain.generation.persistence", .label = "Persistence"},
                    persistence_, 0.01, 1.0, 0.01);
                auto lacunarity = ui.NumericInput<double>(
                    {.id = "terrain.generation.lacunarity", .label = "Lacunarity"},
                    lacunarity_, 0.1, 8.0, 0.1);
                if (seed.changed) seed_ = seed.value;
                if (lattice.changed) lattice_size_ = static_cast<std::uint32_t>(lattice.value);
                if (octaves.changed) octaves_ = static_cast<std::uint32_t>(octaves.value);
                if (persistence.changed) persistence_ = static_cast<float>(persistence.value);
                if (lacunarity.changed) lacunarity_ = static_cast<float>(lacunarity.value);
                if (thermal_erosion_enabled_)
                {
                    ui.Section("THERMAL EROSION");
                    auto talus = ui.Slider<double>(
                        {.id = "terrain.erosion.talus", .label = "Talus angle (deg)"},
                        talus_angle_degrees_, 5.0, 60.0);
                    auto rate = ui.Slider<double>(
                        {.id = "terrain.erosion.rate", .label = "Thermal rate"},
                        thermal_rate_, 0.01, 1.0);
                    auto iterations = ui.Slider<std::int64_t>(
                        {.id = "terrain.erosion.iterations", .label = "Iterations"},
                        thermal_iterations_, 1, 100);
                    if (talus.changed) talus_angle_degrees_ = static_cast<float>(talus.value);
                    if (rate.changed) thermal_rate_ = static_cast<float>(rate.value);
                    if (iterations.changed) thermal_iterations_ = static_cast<std::uint32_t>(iterations.value);
                }
                else ui.Text("Thermal erosion is disabled for this recipe.", UiTextRole::Muted);
                if (ui.Button({.id = "terrain.generation.regenerate", .label = "Regenerate"}).activated && regenerate_)
                    regenerate_(seed_, lattice_size_, octaves_, persistence_, lacunarity_,
                                talus_angle_degrees_, thermal_rate_, thermal_iterations_);
                if (ui.Button({.id = "terrain.generation.cancel", .label = "Cancel"}).activated && cancel_)
                    cancel_();
                if (ui.Button({.id = "terrain.generation.bake", .label = "Bake Native Asset"}).activated && bake_)
                    bake_();
            }
        }
        {
            const auto tab = ui.TabItem({.id = "terrain.controls.execution", .label = "Execution"});
            if (tab.ContentVisible())
            {
                ui.Text("Control the active recipe evaluation.", UiTextRole::Muted);
                if (ui.Button({.id = "terrain.execution.pause", .label = "Pause at node"}).activated && execution_control_)
                    execution_control_(0);
                if (ui.Button({.id = "terrain.execution.step", .label = "Step node"}).activated && execution_control_)
                    execution_control_(1);
                if (ui.Button({.id = "terrain.execution.resume", .label = "Resume"}).activated && execution_control_)
                    execution_control_(2);
                if (ui.Button({.id = "terrain.execution.cancel", .label = "Cancel"}).activated && cancel_)
                    cancel_();
            }
        }
        {
            const auto tab = ui.TabItem({.id = "terrain.controls.diagnostics", .label = "Diagnostics"});
            if (tab.ContentVisible()) RenderDiagnostics(ui);
        }
        {
            const auto tab = ui.TabItem({.id = "terrain.controls.performance", .label = "Performance"});
            if (tab.ContentVisible()) RenderPerformance(ui);
        }
    }

    void TerrainEditor::RenderDiagnostics(editor::ui::UiContext &ui)
    {
        std::lock_guard lock(snapshot_mutex_);
        for (const auto &[node_id, node] : node_diagnostics_)
        {
            ui.Section(node_id);
            for (const auto &[output_name, value] : node.outputs)
            {
                const std::string id = "terrain.diagnostic.field." + node_id + "." + output_name;
                const std::string label = "Select output: " + output_name;
                if (ui.Selectable({.id = id, .label = label},
                                  selected_field_name_ == node_id + "." + output_name).changed && value)
                {
                    auto scalar = AsScalarView(value);
                    if (scalar)
                    {
                        heightfield_ = std::move(scalar);
                        selected_field_name_ = node_id + "." + output_name;
                    }
                }
            }
            for (const auto &[name, value] : node.scalar_metadata)
                Field(ui, name, Number(value, "%.9g"));
            Field(ui, "Evaluation", Number(node.evaluation_time_ms, "%.3f") + " ms");
            Field(ui, "Output bytes", std::to_string(node.output_bytes));
            Field(ui, "Range", "[" + Number(node.minimum_value) + ", " +
                Number(node.maximum_value) + "]");
            Field(ui, "Values", node.all_values_finite ? "finite" : "non-finite");
            char hash[32]{};
            std::snprintf(hash, sizeof(hash), "%016llx",
                static_cast<unsigned long long>(node.content_hash));
            Field(ui, "Content hash", hash);
        }
        Field(ui, "Heightmap selection", selected_field_name_);
    }

    void TerrainEditor::RenderPerformance(editor::ui::UiContext &ui)
    {
        render::RenderSystem *const render_system =
            runtime::global_runtime_context.render_system_.get();
        if (render_system == nullptr)
        {
            ui.Text("Render system is unavailable.", editor::ui::UiTextRole::Warning);
            return;
        }
        const auto profile = render_system->GetMetrics().profile;
        Field(ui, "API", profile.graphics_api == GraphicsAPIType::GRAPHICS_API_VULKAN
            ? "Vulkan" : profile.graphics_api == GraphicsAPIType::GRAPHICS_API_OPENGL
                ? "OpenGL" : "Unknown");
        Field(ui, "Viewport", std::to_string(profile.viewport_width) + " x " +
            std::to_string(profile.viewport_height));
        Field(ui, "CPU total", Number(profile.cpu_total_ms, "%.2f") + " ms");
        Field(ui, "CPU render", Number(profile.cpu_record_ms, "%.2f") + " ms");
        Field(ui, "Draw calls", std::to_string(profile.draw_calls));
        Field(ui, "Sections", std::to_string(profile.sections));
        double gpu_total = 0.0;
        std::size_t gpu_passes = 0;
        for (const auto &pass : profile.passes)
        {
            if (!pass.gpu_time_ms) continue;
            gpu_total += *pass.gpu_time_ms;
            ++gpu_passes;
        }
        Field(ui, "GPU pass total", gpu_passes == 0
            ? "N/A" : Number(gpu_total, "%.2f") + " ms");
        Field(ui, "Timed passes", std::to_string(gpu_passes));
    }

    void TerrainEditor::RenderLog(editor::ui::UiContext &ui)
    {
        if (!log_panel_) return;
        log_panel_->RenderContent(ui, runtime::global_runtime_context.log_system_.get());
    }
}
