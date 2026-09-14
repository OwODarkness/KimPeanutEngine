#ifndef KPENGINE_EDITOR_ASSET_BROWSER_COMPONENT_H
#define KPENGINE_EDITOR_ASSET_BROWSER_COMPONENT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "editor/asset/asset_browser_model.h"
#include "editor/ui/component/editor_window_component.h"

namespace kpengine::editor
{
    // The Asset Browser panel. It is a dock-hosted panel like any other, so it owns no
    // window geometry: the dock host places it, and movability comes from being dragged
    // between docks.
    //
    // Catalog and filtering decisions live in AssetBrowserModel. This class owns only
    // presentation state such as icon pixels, folder highlighting, and ImGui drawing.
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
        void LoadIconImages();

        AssetBrowserModel &model_;
        OpenReferences open_references_;
        float folder_width_{168.0f};
        // Folder highlighting is view state; selecting a folder must not change the asset projection.
        std::string selected_folder_;
        std::unordered_map<std::string, std::vector<std::uint32_t>> icon_pixels_{};
        bool initial_refresh_attempted_{false};
        // Persistent because ImGui edits a buffer in place; the model holds the canonical
        // string and this mirrors it. The location filter has no such buffer — it is read
        // straight from the query, so there is no second copy to fall out of step.
        std::array<char, 128> search_{};
    };
}

#endif // KPENGINE_EDITOR_ASSET_BROWSER_COMPONENT_H
