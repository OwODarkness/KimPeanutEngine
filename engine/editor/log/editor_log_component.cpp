#include "editor/log/editor_log_component.h"

namespace kpengine::editor
{
    namespace
    {
        LogEditorColorTable ToViewColors(const LogLevelColorTable &colors)
        {
            LogEditorColorTable view_colors{};
            for (std::size_t index = 0; index < colors.size(); ++index)
            {
                view_colors[index] = {colors[index].r, colors[index].g,
                                      colors[index].b, colors[index].a};
            }
            return view_colors;
        }
    }

    EditorLogComponent::EditorLogComponent(
        LogSystem *log_system, const LogLevelColorTable &colors,
        EditorWindowConfig config)
        : EditorWindowComponent("OutputLog", config),
          log_system_(log_system), view_(ToViewColors(colors))
    {
    }

    void EditorLogComponent::RenderContent()
    {
        EditorWindowComponent::RenderContent();
        view_.RenderContent(log_system_);
    }
}
