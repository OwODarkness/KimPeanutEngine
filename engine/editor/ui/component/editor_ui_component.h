#ifndef KPENGINE_EDITOR_UI_COMPONENT_H
#define KPENGINE_EDITOR_UI_COMPONENT_H

#include <imgui.h>

#include <optional>

#include "editor/ui/component/editor_layout_model.h"

namespace kpengine::editor
{
    class EditorUIComponent
    {
    public:
        virtual ~EditorUIComponent() = default;
        virtual void Render() = 0;

        // The layout region this component occupies, or nullopt when it places itself:
        // the menu bar, the loading tree, and the Live2D viewer's own panels.
        //
        // Invariant: only a component reached by EditorUI's per-frame layout pass may
        // declare a slot. The tool row's hosted panels must not, because the row draws
        // them inside its own window rather than letting the layout position them.
        virtual std::optional<EditorLayoutSlot> GetLayoutSlot() const noexcept
        {
            return std::nullopt;
        }

        // Pushes this frame's rectangle, or clears it. Taking an optional rather than a
        // rect plus a separate clear means a component that stops being slotted cannot
        // keep last frame's rect and render at stale geometry forever.
        virtual void ApplyLayout(std::optional<EditorRect> rect) noexcept {}
    };

}

#endif
