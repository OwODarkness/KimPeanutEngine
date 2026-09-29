#ifndef KPENGINE_TERRAIN_EDITOR_TERRAIN_EDITOR_H
#define KPENGINE_TERRAIN_EDITOR_TERRAIN_EDITOR_H

#include <memory>
#include <string>

#include "editor/ui/component/editor_layout_model.h"
#include "editor/ui/component/editor_splitter_handles.h"
#include "editor/ui/component/editor_tool_row_model.h"
#include "heightmap_debug_view.h"

namespace kpengine
{
    namespace editor
    {
        class EditorLogComponent;
        class EditorGpuProfilerComponent;
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
                        std::string &diagnostic);
        bool Render(std::string &diagnostic);
        void Shutdown() noexcept;

    private:
        void RenderPanels();
        void RenderTerrainView();
        void RenderControlPanel();
        void ApplyLayout();

        std::shared_ptr<const ScalarField2D> heightfield_;
        std::unique_ptr<editor::EditorUI> ui_;
        std::unique_ptr<editor::EditorLogComponent> log_panel_;
        std::unique_ptr<editor::EditorGpuProfilerComponent> performance_panel_;
        std::unique_ptr<editor::EditorToolRowComponent> dock_host_;
        editor::EditorLayoutModel layout_;
        editor::EditorToolRowModel dock_model_;
        editor::EditorSplitterHandles splitter_handles_;
        HeightmapDebugView heightmap_view_;
    };
}

#endif
