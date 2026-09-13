#ifndef KPENGINE_EDITOR_TOOL_ROW_COMPONENT_H
#define KPENGINE_EDITOR_TOOL_ROW_COMPONENT_H

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "editor/ui/component/editor_tool_row_model.h"
#include "editor/ui/component/editor_window_component.h"

namespace kpengine::editor
{
    // The shared bottom band: one tab strip plus the active docked panel's body,
    // and one standalone window per isolated panel.
    //
    // This component OWNS its panels. They never enter EditorUI::components_, so the
    // top-level render loop cannot draw them twice, and all three of EditorUI's
    // teardown sites (BeginClosing, Close, and the promotion rollback) stay correct
    // without extra cleanup, because the panels die with their container.
    class EditorToolRowComponent final : public EditorWindowComponent
    {
    public:
        // Per-frame upkeep for a panel that is NOT drawn this frame. The console
        // needs this: it drains deferred command results every frame, and a closed
        // tab is not rendered at all.
        using PanelPump = std::function<void()>;

        // The model is borrowed and must outlive this component.
        EditorToolRowComponent(EditorToolRowModel &model, EditorWindowConfig config);

        // Registers the entry, takes ownership of the panel, and binds the panel's
        // visibility to that entry. Returns the owned panel so the caller can give
        // it a pump.
        EditorWindowComponent *AddPanel(std::string id, std::string title,
                                        std::unique_ptr<EditorWindowComponent> panel,
                                        bool open = true, bool docked = true);
        void SetPanelPump(std::string_view id, PanelPump pump);

        void Render() override;

    protected:
        void RenderContent() override;

    public:
        // The row is a container, not a panel: its own title bar must not be able
        // to close it, and it needs no lock toggle.
        bool HasCloseButton() const override { return false; }

    private:
        void RenderTabStrip();
        void RenderDetachedWindows(const std::vector<std::size_t> &detached);
        void RenderDragPreview();

        EditorToolRowModel &model_;
        std::vector<std::unique_ptr<EditorWindowComponent>> panels_;  // parallel to entries
        std::vector<PanelPump> pumps_;                                // parallel to entries
        EditorRect row_rect_{};
        int drag_index_ = -1;     // tab being dragged, -1 when idle
        int context_index_ = -1;  // tab whose context menu is open
    };
}

#endif // KPENGINE_EDITOR_TOOL_ROW_COMPONENT_H
