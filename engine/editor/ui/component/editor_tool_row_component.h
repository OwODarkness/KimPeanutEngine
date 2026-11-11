#ifndef KPENGINE_EDITOR_TOOL_ROW_COMPONENT_H
#define KPENGINE_EDITOR_TOOL_ROW_COMPONENT_H

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <span>
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
        // The model is borrowed and must outlive this component.
        EditorToolRowComponent(EditorToolRowModel &model, EditorWindowConfig config);

        // Registers the entry, takes ownership of the panel, and binds the panel's
        // visibility to that entry. Returns the owned panel so the caller can give
        // it a pump.
        EditorWindowComponent *AddPanel(std::string id, std::string title,
                                        std::unique_ptr<EditorWindowComponent> panel,
                                        bool open = true,
                                        EditorLayoutSlot dock = EditorLayoutSlot::ToolRow);
        bool RequestCloseById(std::string_view id);
        const std::vector<EditorRect> &FloatingBounds() const noexcept { return floating_bounds_; }
        // Borrowed, and must outlive this component: EditorUI declares its layout model
        // before the component vector, so the components are destroyed first. Every dock
        // window's rectangle comes from it. Null is supported and draws nothing.
        void SetLayoutModel(const EditorLayoutModel *layout) noexcept { layout_ = layout; }

        void Render() override;
        void RenderWithContext(ui::UiContext &context) override;
        void RenderOverlay(std::span<const EditorRect> external_occlusions = {});

    private:
        bool IsDrawnThisFrame(std::size_t index) const noexcept;

        // One row container per occupied dock. A container with one panel still uses the same
        // chrome as a multi-panel dock; adding another panel therefore becomes a tab.
        void RenderImpl(ui::UiContext *context);
        void RenderDock(EditorLayoutSlot dock, ui::UiContext *context);
        // A panel standing alone as a floating window.
        void RenderPanelWindow(std::size_t index, const EditorRect &rect, bool pinned,
                               int extra_flags, ui::UiContext *context);
        void RenderFloatingPanels(ui::UiContext *context);
        void RenderTabStrip(EditorLayoutSlot dock, const std::vector<std::size_t> &members);
        // Marks dock destinations during a drag (and empty docks while idle). Drawn before the drag preview
        // so the ghost lands on top of the targets rather than under them.
        void RenderPlacementTargets(std::span<const EditorRect> occlusions);
        void RenderDragPreview(std::span<const EditorRect> occlusions);
        EditorPlacementTarget ResolveTargetAt(float x, float y,
                                              std::span<const EditorRect> occlusions) const noexcept;

        EditorToolRowModel &model_;
        const EditorLayoutModel *layout_ = nullptr;  // borrowed
        std::vector<std::unique_ptr<EditorWindowComponent>> panels_;  // parallel to entries
        std::vector<EditorRect> floating_bounds_;
        EditorRect row_rect_{};
        int drag_index_ = -1;     // panel being dragged, -1 when idle
        int drag_candidate_index_ = -1;
        float drag_press_x_ = 0.0f;
        float drag_press_y_ = 0.0f;
        int context_index_ = -1;  // tab whose context menu is open
    };
}

#endif // KPENGINE_EDITOR_TOOL_ROW_COMPONENT_H
