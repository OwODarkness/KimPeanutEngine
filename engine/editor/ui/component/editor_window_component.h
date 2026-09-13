#ifndef KPENGINE_EDITOR_WINDOW_COMPONENT_H
#define KPENGINE_EDITOR_WINDOW_COMPONENT_H

#include <vector>
#include <string>
#include <memory>
#include "editor/ui/component/editor_ui_component.h"
#include "editor/ui/component/editor_window_visibility.h"


namespace kpengine{
    namespace editor{

        // Initial window geometry as fractions of the viewport work area (0..1).
        // Applied once (ImGuiCond_FirstUseEver), so the window still moves/resizes at runtime.
        struct EditorWindowConfig
        {
            float pos_x_ratio = 0.0f;
            float pos_y_ratio = 0.0f;
            float width_ratio = 1.0f;
            float height_ratio = 1.0f;
            bool locked = true;
            int extra_flags = 0;
        };

        class EditorWindowComponent : public EditorUIComponent{

        public:
            EditorWindowComponent(const std::string& title, EditorWindowConfig config = {});
            virtual ~EditorWindowComponent();
            virtual void Render() override;
            virtual void RenderContent();
            void AddComponent(std::shared_ptr<EditorUIComponent> component);
            void SetLocked(bool locked);
            bool IsLocked() const;

            // Optional borrowed open/closed state. When bound, a host (the tool row)
            // owns visibility and the title-bar close is written through to it instead
            // of latching this window closed forever. Null keeps the standalone
            // behaviour. Not owned: the borrower must outlive this component.
            void SetVisibility(EditorWindowVisibility *visibility) noexcept;
            bool IsVisible() const noexcept;

            // False suppresses the title-bar close button and the chrome that goes
            // with it, so a container host cannot be closed by its own title bar.
            virtual bool HasCloseButton() const { return true; }

        protected:
            void RenderWindowChrome();
            std::string title_;
            EditorWindowConfig config_;
            std::vector<std::shared_ptr<EditorUIComponent>> components_;
            bool is_open_ = true;
            bool locked_;
            bool focused_last_frame_ = false;
            EditorWindowVisibility *visibility_ = nullptr;  // borrowed, not owned
        public:
            int width_{};
            int height_{};
            float pos_x{};
            float pos_y{};
        };
    }
}

#endif
