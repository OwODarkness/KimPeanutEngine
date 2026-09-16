#ifndef KPENGINE_EDITOR_ASSET_REFERENCE_COMPONENT_H
#define KPENGINE_EDITOR_ASSET_REFERENCE_COMPONENT_H

#include <functional>
#include <string_view>

#include "editor/asset/asset_reference_view_model.h"
#include "editor/ui/component/editor_window_component.h"

namespace kpengine::editor
{
    class EditorAssetReferenceComponent final : public EditorWindowComponent
    {
    public:
        explicit EditorAssetReferenceComponent(AssetReferenceViewModel &model,
                                               EditorWindowConfig config = {});

        using LocateInBrowser = std::function<void(std::string_view stable_key)>;
        void SetLocateInBrowser(LocateInBrowser locate_in_browser);

        bool HasCloseButton() const override { return true; }
        void RenderContent() override;

    private:
        void RenderToolbar();
        void RenderTree();
        void RenderText();
        void RenderDetails();
        void RenderEmptyState();

        AssetReferenceViewModel &model_;
        LocateInBrowser locate_in_browser_;
        bool copied_{false};
    };
}

#endif // KPENGINE_EDITOR_ASSET_REFERENCE_COMPONENT_H
