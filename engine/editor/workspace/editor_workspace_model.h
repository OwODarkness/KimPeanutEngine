#ifndef KPENGINE_EDITOR_WORKSPACE_MODEL_H
#define KPENGINE_EDITOR_WORKSPACE_MODEL_H

#include <cstddef>
#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

namespace kpengine::editor
{
    struct WorkspaceRect
    {
        float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
        bool IsEmpty() const noexcept { return width <= 0.0f || height <= 0.0f; }
        bool Contains(float px, float py) const noexcept
        { return px >= x && px <= x + width && py >= y && py <= y + height; }
        bool Overlaps(const WorkspaceRect &other) const noexcept
        { return !IsEmpty() && !other.IsEmpty() && x < other.x + other.width &&
                 other.x < x + width && y < other.y + other.height && other.y < y + height; }
    };

    inline std::vector<WorkspaceRect> SubtractOcclusions(
        const WorkspaceRect &source, const std::span<const WorkspaceRect> occlusions)
    {
        std::vector<WorkspaceRect> exposed;
        if (source.IsEmpty()) return exposed;
        exposed.push_back(source);
        for (const WorkspaceRect &blocker : occlusions)
        {
            std::vector<WorkspaceRect> next;
            for (const WorkspaceRect &piece : exposed)
            {
                if (!piece.Overlaps(blocker)) { next.push_back(piece); continue; }
                const float left = std::max(piece.x, blocker.x), top = std::max(piece.y, blocker.y);
                const float right = std::min(piece.x + piece.width, blocker.x + blocker.width);
                const float bottom = std::min(piece.y + piece.height, blocker.y + blocker.height);
                const WorkspaceRect parts[] = {{piece.x, piece.y, piece.width, top - piece.y},
                    {piece.x, bottom, piece.width, piece.y + piece.height - bottom},
                    {piece.x, top, left - piece.x, bottom - top},
                    {right, top, piece.x + piece.width - right, bottom - top}};
                for (const WorkspaceRect &part : parts) if (!part.IsEmpty()) next.push_back(part);
            }
            exposed = std::move(next);
        }
        return exposed;
    }
    enum class WorkspaceAxis { Horizontal, Vertical };

    struct WorkspaceRegion
    {
        std::string id;
        std::string label;
    };

    struct WorkspaceSplit
    {
        std::string id;
        WorkspaceAxis axis = WorkspaceAxis::Horizontal;
        float fraction = 0.5f;
        float minimum_first = 0.0f;
        float minimum_second = 0.0f;
        int first = -1;
        int second = -1;
    };

    struct WorkspaceLayoutPreset
    {
        std::vector<WorkspaceRegion> regions;
        std::vector<WorkspaceSplit> splits;
        int root = -1;
    };

    struct WorkspacePanelState
    {
        std::string id;
        std::string title;
        std::optional<std::string> region;
        bool open = true;
        WorkspaceRect floating_bounds{};
    };

    class EditorWorkspaceModel final
    {
    public:
        explicit EditorWorkspaceModel(WorkspaceLayoutPreset preset);
        const WorkspaceLayoutPreset &Preset() const noexcept { return preset_; }
        const std::vector<WorkspacePanelState> &Panels() const noexcept { return panels_; }
        std::vector<WorkspacePanelState> &Panels() noexcept { return panels_; }
        bool AddPanel(std::string id, std::string title, std::string region);
        WorkspacePanelState *FindPanel(std::string_view id) noexcept;
        const WorkspacePanelState *FindPanel(std::string_view id) const noexcept;
        bool SetOpen(std::string_view id, bool open);
        bool Select(std::string_view id);
        std::string_view ActivePanel(std::string_view region) const noexcept;
        bool MoveToRegion(std::string_view id, std::string region);
        bool Float(std::string_view id);
        void Resolve(const WorkspaceRect &bounds);
        const WorkspaceRect *RegionBounds(std::string_view id) const noexcept;
        const WorkspaceRect *SplitBounds(std::string_view id) const noexcept;
        std::pair<float, float> SplitFractionRange(std::string_view id) const noexcept;
        bool SetSplitFraction(std::string_view id, float fraction) noexcept;

    private:
        void ResolveNode(int node, const WorkspaceRect &bounds);
        float MinimumExtent(int child, WorkspaceAxis axis) const noexcept;
        WorkspaceLayoutPreset preset_;
        std::vector<WorkspacePanelState> panels_;
        std::vector<WorkspaceRect> region_bounds_;
        std::vector<WorkspaceRect> split_bounds_;
        std::unordered_map<std::string, std::string> active_by_region_;
    };
}

#endif
