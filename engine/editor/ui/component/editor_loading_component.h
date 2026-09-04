#ifndef KPENGINE_EDITOR_LOADING_COMPONENT_H
#define KPENGINE_EDITOR_LOADING_COMPONENT_H

#include <functional>

#include "editor/ui/component/editor_loading_view_model.h"
#include "editor/ui/component/editor_ui_component.h"

namespace kpengine::editor
{
    class EditorLoadingComponent final : public EditorUIComponent
    {
    public:
        explicit EditorLoadingComponent(
            std::function<runtime::StartupSnapshot()> snapshot_source);

        void Render() override;

        const EditorLoadingViewModel &GetLastViewModel() const noexcept
        {
            return last_view_model_;
        }

    private:
        std::function<runtime::StartupSnapshot()> snapshot_source_;
        EditorLoadingViewModel last_view_model_{};
    };
}

#endif // KPENGINE_EDITOR_LOADING_COMPONENT_H
