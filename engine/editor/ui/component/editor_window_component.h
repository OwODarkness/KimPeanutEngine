#ifndef KPENGINE_EDITOR_WINDOW_COMPONENT_H
#define KPENGINE_EDITOR_WINDOW_COMPONENT_H

#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "editor/ui/component/editor_ui_component.h"
#include "editor/ui/component/editor_layout_rect.h"
#include "editor/ui/component/editor_window_visibility.h"


namespace kpengine{
    namespace editor{

        // Initial window geometry as fractions of the viewport work area (0..1).
        // Applied once (ImGuiCond_FirstUseEver), so the window still moves/resizes at runtime.
        struct EditorWindowConfig
        {
            // Used ONLY when the component declares no layout slot: an unslotted window
            // places itself, so it needs geometry. A slotted window's rect comes from the
            // layout each frame and these ratios are ignored.
            float pos_x_ratio = 0.0f;
            float pos_y_ratio = 0.0f;
            float width_ratio = 1.0f;
            float height_ratio = 1.0f;
            bool locked = true;
            int extra_flags = 0;

            // APPENDED deliberately. C++17 has no designated initializers and every
            // construction site in the repo initializes this struct positionally, so a
            // new member may only go after the existing ones: the two sites outside
            // EditorUI (the Live2D viewer's log panel and the startup profiler) then keep
            // compiling and behaving unchanged.
            std::optional<EditorLayoutSlot> slot;
        };

        // Config for a panel the layout places. Its rectangle comes from the layout each
        // frame, so no ratio geometry is carried: a leftover ratio would be a dead value
        // that reads as live. Named construction rather than a positional initializer
        // because the ratios precede `slot` in the struct.
        inline EditorWindowConfig SlotConfig(EditorLayoutSlot slot, int extra_flags = 0)
        {
            EditorWindowConfig config;
            config.slot = slot;
            config.extra_flags = extra_flags;
            return config;
        }

        class EditorWindowComponent : public EditorUIComponent{

        public:
            EditorWindowComponent(const std::string& title, EditorWindowConfig config = {});
            virtual ~EditorWindowComponent();
            virtual void Render() override;
            virtual void RenderContent();
            void AddComponent(std::shared_ptr<EditorUIComponent> component);

            // Optional borrowed open/closed state. When bound, a host (the tool row)
            // owns visibility and the title-bar close is written through to it instead
            // of latching this window closed forever. Null keeps the standalone
            // behaviour. Not owned: the borrower must outlive this component.
            void SetVisibility(EditorWindowVisibility *visibility) noexcept;
            bool IsVisible() const noexcept;

            // Whether the title bar carries a close button. Defaults to false, and must
            // only be overridden where something else can restore the window: a close
            // button is a promise that the window comes back, and a window nothing can
            // reopen is a window the user has destroyed by accident.
            //
            // Today nothing qualifies, and deliberately so:
            //   - a layout-placed panel's region is always present, so dismissing it would
            //     leave a hole in the tiling that nothing fills;
            //   - the loading-tree Startup Profiler and the Live2D viewer's log panel have
            //     no owner that could reopen them.
            // Tool-row tabs are the closable surface, and they route through the row's
            // shared EditorWindowVisibility, which the View menu can restore.
            virtual bool HasCloseButton() const { return false; }

            std::optional<EditorLayoutSlot> GetLayoutSlot() const noexcept override;
            void ApplyLayout(std::optional<EditorRect> rect) noexcept override;

        protected:
            // Split so a layout-placed window keeps the focus accent while losing the
            // padlock: the padlock means "snap back to my own geometry", which is
            // meaningless once the layout owns the rect.
            void RenderFocusAccent();
            void RenderLockToggle();
            std::string title_;
            EditorWindowConfig config_;
            std::vector<std::shared_ptr<EditorUIComponent>> components_;
            bool is_open_ = true;
            bool locked_;
            bool focused_last_frame_ = false;
            EditorWindowVisibility *visibility_ = nullptr;  // borrowed, not owned
            std::optional<EditorRect> layout_rect_;         // pushed by EditorUI each frame
        };
    }
}

#endif
