#ifndef KPENGINE_EDITOR_ASSET_BROWSER_COMPONENT_H
#define KPENGINE_EDITOR_ASSET_BROWSER_COMPONENT_H

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "editor/asset/asset_browser_model.h"
#include "editor/ui/component/editor_window_component.h"

namespace kpengine::editor
{
    // The Asset Browser panel. It is a dock-hosted panel like any other, so it owns no
    // window geometry: the dock host places it, and movability comes from being dragged
    // between docks.
    //
    // Every decision lives in AssetBrowserModel. This class only reads what the model has
    // already decided and draws it, which is deliberate — the browsing rules are the part
    // worth testing, and ImGui glue is the part that has never been testable here.
    class EditorAssetBrowserComponent final : public EditorWindowComponent
    {
    public:
        explicit EditorAssetBrowserComponent(AssetBrowserModel &model);

        // Bound by AB1.3. Until it is, no "Open References" control is rendered at all,
        // rather than one that silently does nothing.
        using OpenReferences = std::function<void(std::string_view stable_key)>;
        void SetOpenReferences(OpenReferences open_references);

        void RenderContent() override;

    private:
        void RenderUnavailable();
        void RenderToolbar();
        void RenderFolderList();
        void RenderTable();
        void RenderTiles();
        void RenderDetails();
        void RenderStatus();

        AssetBrowserModel &model_;
        OpenReferences open_references_;
        float folder_width_{168.0f};
        // Persistent because ImGui edits a buffer in place; the model holds the canonical
        // string and this mirrors it. The location filter has no such buffer — it is read
        // straight from the query, so there is no second copy to fall out of step.
        std::array<char, 128> search_{};
    };
}

#endif // KPENGINE_EDITOR_ASSET_BROWSER_COMPONENT_H
