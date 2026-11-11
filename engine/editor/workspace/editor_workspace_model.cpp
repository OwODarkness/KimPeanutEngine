#include "editor_workspace_model.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace kpengine::editor
{
    namespace
    {
        float ClampFirst(const float desired, const float extent, const float min_first,
                         const float min_second)
        {
            const float total_min = min_first + min_second;
            if (extent <= total_min)
                return total_min > 0.0f ? extent * min_first / total_min : extent * 0.5f;
            return std::clamp(desired, min_first, extent - min_second);
        }
    }

    EditorWorkspaceModel::EditorWorkspaceModel(WorkspaceLayoutPreset preset)
        : preset_(std::move(preset)), region_bounds_(preset_.regions.size()),
          split_bounds_(preset_.splits.size())
    {
        for (std::size_t i = 0; i < preset_.regions.size(); ++i)
        {
            if (preset_.regions[i].id.empty())
                throw std::invalid_argument("workspace region IDs must be nonempty and unique");
            for (std::size_t j = i + 1; j < preset_.regions.size(); ++j)
                if (preset_.regions[i].id == preset_.regions[j].id)
                    throw std::invalid_argument("workspace region IDs must be nonempty and unique");
        }
        if (preset_.root < 0 || static_cast<std::size_t>(preset_.root) >= preset_.splits.size())
            throw std::invalid_argument("workspace preset root split is invalid");
        std::unordered_set<std::string> split_ids;
        for (const WorkspaceSplit &split : preset_.splits)
            if (split.id.empty() || !split_ids.insert(split.id).second ||
                !std::isfinite(split.fraction))
                throw std::invalid_argument("workspace split IDs and fractions must be valid");

        std::vector<bool> visiting(preset_.splits.size());
        std::vector<bool> visited(preset_.splits.size());
        std::vector<bool> regions_seen(preset_.regions.size());
        const auto validate_node = [this, &visiting, &visited, &regions_seen](const auto &self,
                                                                             const int node) -> void
        {
            if (node >= 0)
            {
                const auto index = static_cast<std::size_t>(node);
                if (index >= preset_.splits.size() || visiting[index] || visited[index])
                    throw std::invalid_argument("workspace layout must be an acyclic tree");
                visiting[index] = true;
                const WorkspaceSplit &split = preset_.splits[index];
                self(self, split.first);
                self(self, split.second);
                visiting[index] = false;
                visited[index] = true;
                return;
            }
            if (node == 0)
                throw std::invalid_argument("workspace layout contains an invalid child");
            const std::size_t region = static_cast<std::size_t>(-node - 1);
            if (region >= regions_seen.size() || regions_seen[region])
                throw std::invalid_argument("workspace layout has an invalid or repeated region");
            regions_seen[region] = true;
        };
        validate_node(validate_node, preset_.root);
        if (std::find(visited.begin(), visited.end(), false) != visited.end() ||
            std::find(regions_seen.begin(), regions_seen.end(), false) != regions_seen.end())
            throw std::invalid_argument("workspace layout must place every split and region");
    }

    bool EditorWorkspaceModel::AddPanel(std::string id, std::string title, std::string region)
    {
        const bool known = std::any_of(preset_.regions.begin(), preset_.regions.end(),
            [&region](const WorkspaceRegion &candidate) { return candidate.id == region; });
        if (id.empty() || title.empty() || FindPanel(id) != nullptr || !known) return false;
        panels_.push_back({std::move(id), std::move(title), std::move(region), true});
        active_by_region_.try_emplace(*panels_.back().region, panels_.back().id);
        return true;
    }

    WorkspacePanelState *EditorWorkspaceModel::FindPanel(const std::string_view id) noexcept
    {
        const auto found = std::find_if(panels_.begin(), panels_.end(),
            [id](const WorkspacePanelState &panel) { return panel.id == id; });
        return found == panels_.end() ? nullptr : &*found;
    }

    const WorkspacePanelState *EditorWorkspaceModel::FindPanel(const std::string_view id) const noexcept
    {
        const auto found = std::find_if(panels_.begin(), panels_.end(),
            [id](const WorkspacePanelState &panel) { return panel.id == id; });
        return found == panels_.end() ? nullptr : &*found;
    }

    bool EditorWorkspaceModel::SetOpen(const std::string_view id, const bool open)
    {
        if (auto *panel = FindPanel(id))
        {
            panel->open = open;
            if (open && panel->region) active_by_region_[*panel->region] = panel->id;
            else if (panel->region && ActivePanel(*panel->region) == panel->id)
            {
                active_by_region_.erase(*panel->region);
                const auto replacement = std::find_if(panels_.begin(), panels_.end(),
                    [panel](const WorkspacePanelState &candidate)
                    {
                        return candidate.open && candidate.region == panel->region;
                    });
                if (replacement != panels_.end())
                    active_by_region_[*panel->region] = replacement->id;
            }
            return true;
        }
        return false;
    }

    bool EditorWorkspaceModel::Select(const std::string_view id)
    {
        auto *panel = FindPanel(id);
        if (panel == nullptr || !panel->region || !panel->open) return false;
        active_by_region_[*panel->region] = panel->id;
        return true;
    }

    std::string_view EditorWorkspaceModel::ActivePanel(const std::string_view region) const noexcept
    {
        const auto found = std::find_if(active_by_region_.begin(), active_by_region_.end(),
            [region](const auto &entry) { return entry.first == region; });
        return found == active_by_region_.end() ? std::string_view{} : found->second;
    }

    bool EditorWorkspaceModel::MoveToRegion(const std::string_view id, std::string region)
    {
        const bool known = std::any_of(preset_.regions.begin(), preset_.regions.end(),
            [&region](const WorkspaceRegion &candidate) { return candidate.id == region; });
        auto *panel = FindPanel(id);
        if (!known || panel == nullptr) return false;
        if (panel->region)
        {
            const WorkspaceRect *old_bounds = RegionBounds(*panel->region);
            if (old_bounds != nullptr) panel->floating_bounds = *old_bounds;
            const auto active = active_by_region_.find(*panel->region);
            if (active != active_by_region_.end() && active->second == panel->id)
            {
                active_by_region_.erase(active);
                const auto replacement = std::find_if(panels_.begin(), panels_.end(),
                    [panel](const WorkspacePanelState &candidate)
                    {
                        return candidate.id != panel->id && candidate.open &&
                               candidate.region == panel->region;
                    });
                if (replacement != panels_.end()) active_by_region_[*panel->region] = replacement->id;
            }
        }
        panel->region = std::move(region);
        panel->open = true;
        active_by_region_[*panel->region] = panel->id;
        return true;
    }

    bool EditorWorkspaceModel::Float(const std::string_view id)
    {
        auto *panel = FindPanel(id);
        if (panel == nullptr) return false;
        if (panel->region)
        {
            const WorkspaceRect *bounds = RegionBounds(*panel->region);
            if (bounds != nullptr) panel->floating_bounds = *bounds;
            const auto active = active_by_region_.find(*panel->region);
            if (active != active_by_region_.end() && active->second == panel->id)
            {
                active_by_region_.erase(active);
                const auto replacement = std::find_if(panels_.begin(), panels_.end(),
                    [panel](const WorkspacePanelState &candidate)
                    {
                        return candidate.id != panel->id && candidate.open &&
                               candidate.region == panel->region;
                    });
                if (replacement != panels_.end()) active_by_region_[*panel->region] = replacement->id;
            }
        }
        panel->region.reset();
        panel->open = true;
        return true;
    }

    void EditorWorkspaceModel::Resolve(const WorkspaceRect &bounds)
    {
        std::fill(region_bounds_.begin(), region_bounds_.end(), WorkspaceRect{});
        std::fill(split_bounds_.begin(), split_bounds_.end(), WorkspaceRect{});
        ResolveNode(preset_.root, bounds);
    }

    float EditorWorkspaceModel::MinimumExtent(const int child, const WorkspaceAxis axis) const noexcept
    {
        if (child < 0 || static_cast<std::size_t>(child) >= preset_.splits.size()) return 0.0f;
        const WorkspaceSplit &split = preset_.splits[static_cast<std::size_t>(child)];
        const float first = MinimumExtent(split.first, axis);
        const float second = MinimumExtent(split.second, axis);
        if (split.axis == axis)
            return std::max(split.minimum_first, first) + std::max(split.minimum_second, second);
        return std::max(first, second);
    }

    void EditorWorkspaceModel::ResolveNode(const int node, const WorkspaceRect &bounds)
    {
        if (node < 0 || static_cast<std::size_t>(node) >= preset_.splits.size()) return;
        const WorkspaceSplit &split = preset_.splits[static_cast<std::size_t>(node)];
        split_bounds_[static_cast<std::size_t>(node)] = bounds;
        const bool horizontal = split.axis == WorkspaceAxis::Horizontal;
        const float extent = horizontal ? bounds.width : bounds.height;
        const float minimum_first = std::max(split.minimum_first, MinimumExtent(split.first, split.axis));
        const float minimum_second = std::max(split.minimum_second, MinimumExtent(split.second, split.axis));
        const float first_extent = ClampFirst(extent * split.fraction, extent,
                                              minimum_first, minimum_second);
        WorkspaceRect first = bounds, second = bounds;
        if (horizontal)
        {
            first.width = first_extent;
            second.x += first_extent;
            second.width -= first_extent;
        }
        else
        {
            first.height = first_extent;
            second.y += first_extent;
            second.height -= first_extent;
        }
        const auto resolve_child = [this](const int child, const WorkspaceRect &rect)
        {
            if (child >= 0) ResolveNode(child, rect);
            else
            {
                const std::size_t region = static_cast<std::size_t>(-child - 1);
                if (region < region_bounds_.size()) region_bounds_[region] = rect;
            }
        };
        resolve_child(split.first, first);
        resolve_child(split.second, second);
    }

    const WorkspaceRect *EditorWorkspaceModel::RegionBounds(const std::string_view id) const noexcept
    {
        const auto found = std::find_if(preset_.regions.begin(), preset_.regions.end(),
            [id](const WorkspaceRegion &region) { return region.id == id; });
        return found == preset_.regions.end() ? nullptr :
            &region_bounds_[static_cast<std::size_t>(found - preset_.regions.begin())];
    }

    const WorkspaceRect *EditorWorkspaceModel::SplitBounds(const std::string_view id) const noexcept
    {
        const auto found = std::find_if(preset_.splits.begin(), preset_.splits.end(),
            [id](const WorkspaceSplit &split) { return split.id == id; });
        return found == preset_.splits.end() ? nullptr :
            &split_bounds_[static_cast<std::size_t>(found - preset_.splits.begin())];
    }

    std::pair<float, float> EditorWorkspaceModel::SplitFractionRange(
        const std::string_view id) const noexcept
    {
        const auto found = std::find_if(preset_.splits.begin(), preset_.splits.end(),
            [id](const WorkspaceSplit &split) { return split.id == id; });
        if (found == preset_.splits.end()) return {0.0f, 1.0f};
        const std::size_t index = static_cast<std::size_t>(found - preset_.splits.begin());
        const WorkspaceRect &bounds = split_bounds_[index];
        const float extent = found->axis == WorkspaceAxis::Horizontal ? bounds.width : bounds.height;
        const float minimum_first = std::max(found->minimum_first, MinimumExtent(found->first, found->axis));
        const float minimum_second = std::max(found->minimum_second, MinimumExtent(found->second, found->axis));
        if (extent <= 0.0f) return {0.0f, 1.0f};
        if (extent <= minimum_first + minimum_second)
        {
            const float fixed = minimum_first + minimum_second > 0.0f
                ? minimum_first / (minimum_first + minimum_second) : 0.5f;
            return {fixed, fixed};
        }
        return {minimum_first / extent, 1.0f - minimum_second / extent};
    }

    bool EditorWorkspaceModel::SetSplitFraction(const std::string_view id,
                                                const float fraction) noexcept
    {
        const auto found = std::find_if(preset_.splits.begin(), preset_.splits.end(),
            [id](const WorkspaceSplit &split) { return split.id == id; });
        if (found == preset_.splits.end() || !std::isfinite(fraction)) return false;
        const auto [minimum, maximum] = SplitFractionRange(id);
        found->fraction = std::clamp(fraction, minimum, maximum);
        return true;
    }
}
