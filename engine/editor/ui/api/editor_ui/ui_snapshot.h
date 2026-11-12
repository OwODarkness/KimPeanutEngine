#ifndef KPENGINE_EDITOR_UI_API_UI_SNAPSHOT_H
#define KPENGINE_EDITOR_UI_API_UI_SNAPSHOT_H

#include "ui_backend.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace kpengine::editor::ui
{
    enum class UiWorkspaceAxis : std::uint8_t { Horizontal, Vertical };

    struct UiWorkspaceRegionSnapshot
    {
        std::string id;
        std::string label;
        UiRect bounds{};
        bool has_bounds = false;
        std::vector<std::string> tabs;
        std::optional<std::string> active_tab;
    };

    struct UiWorkspaceSplitSnapshot
    {
        std::string id;
        UiWorkspaceAxis axis = UiWorkspaceAxis::Horizontal;
        UiRect bounds{};
        bool has_bounds = false;
        std::vector<std::string> children;
        float requested_ratio = 0.5F;
        float resolved_ratio = 0.5F;
    };

    struct UiWorkspacePanelSnapshot
    {
        std::string id;
        std::string title;
        std::optional<std::string> region;
        bool open = false;
        bool active = false;
        UiRect bounds{};
        bool has_bounds = false;
    };

    struct UiWorkspaceSnapshot
    {
        std::string id;
        std::string root;
        std::vector<UiWorkspaceRegionSnapshot> regions;
        std::vector<UiWorkspaceSplitSnapshot> splits;
        std::vector<UiWorkspacePanelSnapshot> panels;
    };

    struct UiSnapshotCapture
    {
        UiSessionId session = 0;
        std::uint64_t frame = 0;
        std::string host;
        std::string mode;
        UiViewportInfo viewport{};
        std::vector<UiNodeObservation> nodes;
        std::vector<UiThemeObservation> styles;
        std::vector<UiWorkspaceSnapshot> workspaces;
        std::size_t virtual_list_total = 0;
        std::size_t virtual_list_first = 0;
        std::size_t virtual_list_last = 0;
        bool has_virtual_list = false;
        bool partial_coverage = false;
        std::size_t omitted_nodes = 0;
        std::vector<std::string> coverage_reasons;
    };

    class UiSnapshotStore final
    {
    public:
        void Publish(UiSnapshotCapture capture);
        std::shared_ptr<const UiSnapshotCapture> Latest(UiSessionId session) const;
        void Clear(UiSessionId session) noexcept;

    private:
        mutable std::mutex mutex_;
        std::shared_ptr<const UiSnapshotCapture> latest_;
    };
}

#endif
