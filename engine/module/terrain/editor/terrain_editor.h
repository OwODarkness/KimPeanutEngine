#ifndef KPENGINE_TERRAIN_EDITOR_TERRAIN_EDITOR_H
#define KPENGINE_TERRAIN_EDITOR_TERRAIN_EDITOR_H

#include <memory>
#include <functional>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "editor/ui/component/editor_layout_model.h"
#include "editor/ui/component/editor_splitter_handles.h"
#include "editor/ui/component/editor_tool_row_model.h"
#include "heightmap_debug_view.h"
#include "evaluation/terrain_generation.h"

namespace kpengine
{
    namespace editor
    {
        class EditorLogComponent;
        class EditorGpuProfilerComponent;
        class EditorProfileBarComponent;
        class EditorToolRowComponent;
        class EditorUI;
    }
    namespace runtime
    {
        class Engine;
    }
    namespace terrain
    {
        class ScalarField2D;
    }
}

namespace kpengine::terrain
{
    class TerrainEditor final
    {
    public:
        TerrainEditor();
        ~TerrainEditor();

        TerrainEditor(const TerrainEditor &) = delete;
        TerrainEditor &operator=(const TerrainEditor &) = delete;

        bool Initialize(runtime::Engine &engine,
                        std::shared_ptr<const ScalarField2D> heightfield,
                        std::string &diagnostic,
                        std::function<void(std::uint64_t, std::uint32_t, std::uint32_t, float, float,
                                           float, float, std::uint32_t)> regenerate = {},
                        std::function<void()> cancel = {},
                        std::function<void(int)> execution_control = {},
                        std::function<void(float, float, float)> camera_control = {},
                        std::function<void()> bake = {});
        void SetEvaluationSnapshot(std::shared_ptr<const ScalarField2D> heightfield,
                                   const EvaluationResult &result,
                                   const TerrainRecipe &recipe, std::string status);
        bool Render(std::string &diagnostic);
        void Shutdown() noexcept;

    private:
        void RenderPanels();
        void RenderTerrainView();
        void RenderControlPanel();
        void ApplyLayout();

        std::shared_ptr<const ScalarField2D> heightfield_;
        std::shared_ptr<const ScalarField2D> pre_erosion_heightfield_;
        std::unique_ptr<editor::EditorUI> ui_;
        std::unique_ptr<editor::EditorLogComponent> log_panel_;
        std::unique_ptr<editor::EditorGpuProfilerComponent> performance_panel_;
        std::unique_ptr<editor::EditorProfileBarComponent> profile_bar_;
        std::unique_ptr<editor::EditorToolRowComponent> dock_host_;
        editor::EditorLayoutModel layout_;
        editor::EditorToolRowModel dock_model_;
        editor::EditorSplitterHandles splitter_handles_;
        HeightmapDebugView heightmap_view_;
        std::function<void(std::uint64_t, std::uint32_t, std::uint32_t, float, float,
                           float, float, std::uint32_t)> regenerate_;
        std::function<void()> cancel_;
        std::function<void(int)> execution_control_;
        std::function<void(float, float, float)> camera_control_;
        std::function<void()> bake_;
        float camera_yaw_degrees_ = -90.0f;
        float camera_pitch_degrees_ = -27.0f;
        float camera_distance_ = 440.0f;
        std::uint64_t seed_ = 128;
        std::uint32_t lattice_size_ = 4;
        std::uint32_t octaves_ = 11;
        float persistence_ = 0.5f;
        float lacunarity_ = 2.0f;
        float talus_angle_degrees_ = 30.0f;
        float thermal_rate_ = 0.25f;
        std::uint32_t thermal_iterations_ = 48;
        bool thermal_erosion_enabled_ = false;
        std::string generation_status_ = "Ready";
        std::string generation_diagnostic_;
        std::string selected_field_name_ = "height_scale.height";
        std::map<std::string, NodeResult, std::less<>> node_diagnostics_;
        mutable std::mutex snapshot_mutex_;
    };
}

#endif
