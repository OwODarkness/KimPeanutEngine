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
        //
        // This describes a window that PLACES ITSELF. A panel hosted by the dock host never
        // reads it: the host owns that panel's rectangle, and reads its own config for the
        // one thing it still places — the window it draws a dock's members inside.
        struct EditorWindowConfig
        {
            float pos_x_ratio = 0.0f;
            float pos_y_ratio = 0.0f;
            float width_ratio = 1.0f;
            float height_ratio = 1.0f;
            bool locked = true;
            int extra_flags = 0;

            // APPENDED deliberately. C++17 has no designated initializers and the
            // construction sites outside EditorUI (the Live2D viewer's log panel and the
            // startup profiler) initialize this struct positionally, so a new member may
            // only go after the existing ones.
            int reserved = 0;
        };

        class EditorWindowComponent : public EditorUIComponent{

        public:
            EditorWindowComponent(const std::string& title, EditorWindowConfig config = {});
            virtual ~EditorWindowComponent();
            virtual void Render() override;
            virtual void RenderContent();
            void AddComponent(std::shared_ptr<EditorUIComponent> component);

            // Optional borrowed open/closed state. When bound, a host (the dock host)
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
            // Today nothing qualifies: the loading-tree Startup Profiler and the Live2D
            // viewer's log panel have no owner that could reopen them, and every workspace
            // panel is closed and reopened from its tab or the View menu.
            virtual bool HasCloseButton() const { return false; }

        protected:
            // The title-bar focus accent: a 2 px strip, NavHighlight when focused.
            void RenderFocusAccent();

            // Draws the padlock in the title bar and reports a click, leaving the flag to
            // the caller. Shared with the dock host, whose panels carry the same padlock
            // on their title bars while their lock lives in the placement model rather
            // than in this component — one padlock drawing, two owners of the bool.
            bool RenderLockButton(bool locked);

            std::string title_;
            EditorWindowConfig config_;
            std::vector<std::shared_ptr<EditorUIComponent>> components_;
            bool is_open_ = true;
            bool locked_;
            bool focused_last_frame_ = false;
            EditorWindowVisibility *visibility_ = nullptr;  // borrowed, not owned
        };
    }
}

#endif
