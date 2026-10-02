#ifndef KPENGINE_EDITOR_LOADING_COMPONENT_H
#define KPENGINE_EDITOR_LOADING_COMPONENT_H

#include <functional>
#include <cstdint>
#include <vector>

#include "editor/ui/component/editor_loading_view_model.h"
#include "editor/ui/component/editor_ui_component.h"

namespace kpengine::editor
{
    class EditorLoadingComponent final : public EditorUIComponent
    {
    public:
        explicit EditorLoadingComponent(
            std::function<runtime::StartupSnapshot()> snapshot_source,
            const bool *glow_enabled = nullptr);

        void Render() override;

        const EditorLoadingViewModel &GetLastViewModel() const noexcept
        {
            return last_view_model_;
        }

    private:
        void LoadIconPixels();

        std::function<runtime::StartupSnapshot()> snapshot_source_;
        const bool *glow_enabled_ = nullptr;
        EditorLoadingViewModel last_view_model_{};
        std::vector<uint32_t> icon_pixels_;
        static constexpr uint32_t kIconSize = 56;
    };
}

#endif // KPENGINE_EDITOR_LOADING_COMPONENT_H
