#include "editor_ui/ui_semantic_interface.h"
#include "editor_ui/ui_backend.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace kpengine::editor::ui
{
    struct UiSemanticSnapshot
    {
        UiFrameResult frame;
        UiSemanticTree tree;
        std::vector<UiElementStyle> styles;
        std::unordered_map<UiNodeId, std::size_t> node_indices;
        std::unordered_map<std::string, std::size_t> semantic_indices;
        std::unordered_map<std::string, std::size_t> style_indices;
    };

    namespace
    {
        constexpr std::uint16_t Field(const UiChangeField field) noexcept
        { return static_cast<std::uint16_t>(field); }

        bool SameOptionalRect(const std::optional<UiRect> &a, const std::optional<UiRect> &b)
        {
            if (a.has_value() != b.has_value()) return false;
            if (!a) return true;
            return a->min.x == b->min.x && a->min.y == b->min.y &&
                   a->max.x == b->max.x && a->max.y == b->max.y;
        }

        std::uint16_t ChangedFields(const UiNodeObservation &a, const UiNodeObservation &b)
        {
            std::uint16_t fields = 0;
            if (a.parent != b.parent) fields |= Field(UiChangeField::Structure);
            if (a.role != b.role || a.label != b.label || a.description != b.description ||
                a.semantic_key != b.semantic_key || a.unit != b.unit ||
                a.domain_summary != b.domain_summary || a.disabled_reason != b.disabled_reason)
                fields |= Field(UiChangeField::Semantics);
            if (a.enabled != b.enabled || a.selected != b.selected || a.active != b.active ||
                a.focused != b.focused || a.hovered != b.hovered || a.visible != b.visible ||
                a.clipped != b.clipped || a.expanded != b.expanded)
                fields |= Field(UiChangeField::State);
            if (a.value != b.value) fields |= Field(UiChangeField::Value);
            if (a.bounds.min.x != b.bounds.min.x || a.bounds.min.y != b.bounds.min.y ||
                a.bounds.max.x != b.bounds.max.x || a.bounds.max.y != b.bounds.max.y ||
                a.geometry != b.geometry || !SameOptionalRect(a.clipped_bounds, b.clipped_bounds))
                fields |= Field(UiChangeField::Geometry);
            if (a.style_id != b.style_id) fields |= Field(UiChangeField::Style);
            if (a.actions != b.actions || a.last_action != b.last_action)
                fields |= Field(UiChangeField::Actions);
            bool choices_changed = a.choices.size() != b.choices.size();
            for (std::size_t i = 0; !choices_changed && i < a.choices.size(); ++i)
                choices_changed = a.choices[i].key != b.choices[i].key ||
                    a.choices[i].label != b.choices[i].label || a.choices[i].value != b.choices[i].value;
            if (a.minimum != b.minimum || a.maximum != b.maximum || a.step != b.step || choices_changed)
                fields |= Field(UiChangeField::Constraints);
            if (a.table_columns != b.table_columns || a.table_column != b.table_column)
                fields |= Field(UiChangeField::Table);
            if (a.chart.has_value() != b.chart.has_value()) fields |= Field(UiChangeField::Chart);
            else if (a.chart && b.chart && (a.chart->x.minimum != b.chart->x.minimum ||
                a.chart->x.maximum != b.chart->x.maximum || a.chart->y.minimum != b.chart->y.minimum ||
                a.chart->y.maximum != b.chart->y.maximum || a.chart->series.size() != b.chart->series.size()))
                fields |= Field(UiChangeField::Chart);
            return fields;
        }

        UiRect Union(const UiRect a, const UiRect b) noexcept
        {
            return {{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y)},
                    {std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y)}};
        }

        bool SameRect(const UiRect &a, const UiRect &b) noexcept
        {
            return a.min.x == b.min.x && a.min.y == b.min.y &&
                   a.max.x == b.max.x && a.max.y == b.max.y;
        }

        bool HasSelectionState(const UiRole role) noexcept
        {
            return role == UiRole::Selectable || role == UiRole::Tab ||
                   role == UiRole::Panel || role == UiRole::Row;
        }

        bool HasInteractionState(const UiRole role) noexcept
        {
            switch (role)
            {
            case UiRole::Button: case UiRole::CheckBox: case UiRole::Selectable:
            case UiRole::TextInput: case UiRole::NumericInput: case UiRole::Slider:
            case UiRole::Combo: case UiRole::Tab: case UiRole::Panel:
                return true;
            default:
                return false;
            }
        }

        UiSemanticElement MakeSemanticElement(const UiNodeObservation &node)
        {
            UiSemanticElement element{};
            element.element_id = node.id;
            element.parent_id = node.parent;
            element.semantic_id = node.semantic_key;
            element.role = node.role;
            element.title = node.label;
            element.description = node.description;
            element.value = node.value;
            element.enabled = node.enabled;
            element.disabled_reason = node.disabled_reason;
            if (HasSelectionState(node.role)) element.selected = node.selected;
            if (HasInteractionState(node.role))
            {
                element.active = node.active;
                element.focused = node.focused;
                element.hovered = node.hovered;
            }
            element.visible = node.visible;
            element.clipped = node.clipped;
            element.expanded = node.expanded;
            element.actions = node.actions;
            element.last_action = node.last_action;
            element.unit = node.unit;
            element.domain_summary = node.domain_summary;
            element.minimum = node.minimum;
            element.maximum = node.maximum;
            element.step = node.step;
            element.choices = node.choices;
            element.table_columns = node.table_columns;
            element.table_column = node.table_column;
            element.chart = node.chart;
            if (element.chart)
                for (auto &series : element.chart->series) series.points.clear();
            if (!node.style_id.empty()) element.style_id = node.style_id;
            return element;
        }

        void BuildTree(UiSemanticSnapshot &snapshot)
        {
            UiSemanticTree &tree = snapshot.tree;
            const UiFrameResult &frame = snapshot.frame;
            tree.session = frame.session;
            tree.frame = frame.frame;
            tree.complete = frame.complete;
            tree.viewport = frame.viewport;
            if (!frame.complete) return;

            tree.nodes.reserve(frame.observations.size());
            snapshot.node_indices.reserve(frame.observations.size());
            for (const UiNodeObservation &observation : frame.observations)
            {
                UiSemanticTreeNode node{};
                node.id = observation.id;
                node.parent = observation.parent;
                node.role = observation.role;
                node.label = observation.label;
                node.bounds = observation.bounds;
                node.geometry = observation.geometry;
                node.clipped_bounds = observation.clipped_bounds;
                node.enabled = observation.enabled;
                node.selected = observation.selected;
                node.active = observation.active;
                node.focused = observation.focused;
                node.hovered = observation.hovered;
                node.clipped = observation.clipped;
                node.visible = observation.visible;
                snapshot.node_indices.emplace(node.id, tree.nodes.size());
                if (!observation.semantic_key.empty())
                    snapshot.semantic_indices.emplace(observation.semantic_key, tree.nodes.size());
                tree.nodes.push_back(std::move(node));
            }

            for (const UiSemanticTreeNode &node : tree.nodes)
            {
                if (node.parent == 0)
                {
                    tree.roots.push_back(node.id);
                    continue;
                }
                const auto parent = snapshot.node_indices.find(node.parent);
                if (parent != snapshot.node_indices.end())
                    tree.nodes[parent->second].children.push_back(node.id);
                else
                    tree.roots.push_back(node.id);
            }

            // Logical containers inherit a measured content extent, clearly marked
            // as derived rather than as a backend-measured border rectangle.
            for (auto node_it = tree.nodes.rbegin(); node_it != tree.nodes.rend(); ++node_it)
            {
                UiSemanticTreeNode &node = *node_it;
                if (node.geometry != UiGeometryProvenance::Unavailable || node.children.empty())
                    continue;
                bool has_bounds = false;
                bool any_visible = false;
                bool any_clipped = false;
                UiRect bounds{};
                std::optional<UiRect> clipped_bounds;
                for (const UiNodeId child_id : node.children)
                {
                    const auto child_index = snapshot.node_indices.find(child_id);
                    if (child_index == snapshot.node_indices.end()) continue;
                    const UiSemanticTreeNode &child = tree.nodes[child_index->second];
                    if (child.geometry != UiGeometryProvenance::Unavailable)
                    {
                        bounds = has_bounds ? Union(bounds, child.bounds) : child.bounds;
                        has_bounds = true;
                    }
                    any_visible = any_visible || child.visible.value_or(false);
                    any_clipped = any_clipped || child.clipped.value_or(false);
                    if (child.clipped_bounds)
                        clipped_bounds = clipped_bounds
                            ? std::optional<UiRect>{Union(*clipped_bounds, *child.clipped_bounds)}
                            : child.clipped_bounds;
                }
                if (!has_bounds) continue;
                node.bounds = bounds;
                node.geometry = UiGeometryProvenance::DerivedFromChildren;
                node.clipped_bounds = clipped_bounds;
                node.visible = any_visible;
                node.clipped = any_clipped;
            }

            snapshot.style_indices.reserve(frame.styles.size());
            snapshot.styles.reserve(frame.styles.size());
            for (std::size_t index = 0; index < frame.styles.size(); ++index)
            {
                const UiThemeObservation &style = frame.styles[index];
                snapshot.style_indices.emplace(style.id, index);
                snapshot.styles.push_back({style.id, style.declared, false});
            }
        }
    }

    bool UiSemanticView::IsValid() const noexcept
    {
        return snapshot_ && snapshot_->frame.complete;
    }

    UiSessionId UiSemanticView::SessionId() const noexcept
    {
        return snapshot_ ? snapshot_->frame.session : 0;
    }

    std::uint64_t UiSemanticView::FrameId() const noexcept
    {
        return snapshot_ ? snapshot_->frame.frame : 0;
    }

    const UiFrameResult &UiSemanticView::FrameResult() const noexcept
    {
        static const UiFrameResult empty{};
        return snapshot_ ? snapshot_->frame : empty;
    }

    const UiSemanticTree &UiSemanticView::GetUITree() const noexcept
    {
        static const UiSemanticTree empty{};
        return snapshot_ ? snapshot_->tree : empty;
    }

    std::optional<UiElementInfo> UiSemanticView::GetElementInfo(const UiElementId id) const
    {
        if (!IsValid()) return std::nullopt;
        const auto found = snapshot_->node_indices.find(id);
        if (found == snapshot_->node_indices.end()) return std::nullopt;
        const UiSemanticTreeNode &node = snapshot_->tree.nodes[found->second];
        const UiNodeObservation &observation = snapshot_->frame.observations[found->second];
        return UiElementInfo{&observation, node.children};
    }

    const UiElementStyle *UiSemanticView::GetElementStyle(const UiElementId id) const noexcept
    {
        if (!IsValid()) return nullptr;
        const auto node = snapshot_->node_indices.find(id);
        if (node == snapshot_->node_indices.end()) return nullptr;
        const std::string &style_id = snapshot_->frame.observations[node->second].style_id;
        const auto style = snapshot_->style_indices.find(style_id);
        if (style_id.empty() || style == snapshot_->style_indices.end()) return nullptr;
        return &snapshot_->styles[style->second];
    }

    UiSemanticQueryResult UiSemanticView::Query(const UiSemanticQuery &query) const
    {
        UiSemanticQueryResult result{};
        result.level = query.level;
        if (!snapshot_) return result;
        if (!snapshot_->frame.complete)
        { result.status = UiQueryStatus::Unavailable; return result; }
        result.status = UiQueryStatus::Ok;
        result.session = SessionId();
        result.frame = FrameId();
        if (query.level != UiQueryLevel::Semantic) result.viewport = snapshot_->frame.viewport;

        UiNodeId root = query.root_id.value_or(0);
        if (query.element_id)
        {
            const auto found = snapshot_->node_indices.find(*query.element_id);
            if (found == snapshot_->node_indices.end()) { result.status = UiQueryStatus::NotFound; return result; }
            root = *query.element_id;
        }
        if (query.semantic_id)
        {
            const auto found = snapshot_->semantic_indices.find(*query.semantic_id);
            if (found == snapshot_->semantic_indices.end()) { result.status = UiQueryStatus::NotFound; return result; }
            const UiNodeId semantic_root = snapshot_->tree.nodes[found->second].id;
            if (root != 0 && root != semantic_root) { result.status = UiQueryStatus::NotFound; return result; }
            root = semantic_root;
        }
        if (root != 0 && !snapshot_->node_indices.contains(root))
        { result.status = UiQueryStatus::NotFound; return result; }

        std::vector<UiNodeId> selected;
        if (root == 0)
        {
            selected.reserve(snapshot_->tree.nodes.size());
            for (const UiSemanticTreeNode &node : snapshot_->tree.nodes) selected.push_back(node.id);
        }
        else
        {
            selected.push_back(root);
            if (query.descendants)
                for (std::size_t i = 0; i < selected.size(); ++i)
                {
                    const auto index = snapshot_->node_indices.find(selected[i]);
                    if (index != snapshot_->node_indices.end())
                        for (UiNodeId child : snapshot_->tree.nodes[index->second].children)
                            selected.push_back(child);
                }
        }
        bool filtered_out = false;
        bool filtered_as_invisible = false;
        for (UiNodeId id : selected)
        {
            const auto index = snapshot_->node_indices.find(id);
            if (index == snapshot_->node_indices.end()) continue;
            const UiNodeObservation &node = snapshot_->frame.observations[index->second];
            if (query.role && node.role != *query.role) { filtered_out = true; continue; }
            if (query.enabled && node.enabled != *query.enabled) { filtered_out = true; continue; }
            if (query.selected && node.selected != query.selected) { filtered_out = true; continue; }
            if (query.active && node.active != query.active) { filtered_out = true; continue; }
            if (query.focused && node.focused != query.focused) { filtered_out = true; continue; }
            if (query.visible && node.visible != query.visible)
            {
                filtered_out = true;
                filtered_as_invisible = filtered_as_invisible ||
                    (query.visible.value() && node.visible.has_value() && !node.visible.value());
                continue;
            }
            UiSemanticElement element = MakeSemanticElement(node);
            if (query.level == UiQueryLevel::Visual) element.chart = node.chart;
            else if (element.chart)
                for (auto &series : element.chart->series) series.points.clear();
            if (query.level != UiQueryLevel::Semantic)
            {
                if (node.geometry != UiGeometryProvenance::Unavailable) element.bounds = node.bounds;
                if (node.clipped_bounds &&
                    (node.geometry == UiGeometryProvenance::Unavailable ||
                     !SameRect(node.bounds, *node.clipped_bounds)))
                    element.clipped_bounds = node.clipped_bounds;
                element.geometry = node.geometry;
            }
            if (query.level == UiQueryLevel::Visual && !node.style_id.empty())
            {
                element.style_id = node.style_id;
                if (const auto style = snapshot_->style_indices.find(node.style_id);
                    style != snapshot_->style_indices.end())
                    result.styles.push_back(snapshot_->styles[style->second]);
            }
            else element.style_id.reset();
            result.elements.push_back(std::move(element));
        }
        if (result.elements.empty())
            result.status = filtered_as_invisible ? UiQueryStatus::Invisible :
                filtered_out ? UiQueryStatus::FilteredOut : UiQueryStatus::NotFound;
        std::sort(result.styles.begin(), result.styles.end(),
            [](const UiElementStyle &a, const UiElementStyle &b) { return a.id < b.id; });
        result.styles.erase(std::unique(result.styles.begin(), result.styles.end(),
            [](const UiElementStyle &a, const UiElementStyle &b) { return a.id == b.id; }), result.styles.end());
        return result;
    }

    void UiSemanticInterface::Update(const UiFrameResult &frame)
    {
        if (!frame.complete || !frame.observed) return;
        auto snapshot = std::make_shared<UiSemanticSnapshot>();
        snapshot->frame = frame;
        BuildTree(*snapshot);
        std::scoped_lock lock(mutex_);
        if (latest_ && latest_->frame.session == snapshot->frame.session)
        {
            std::unordered_map<UiNodeId, const UiNodeObservation *> old_nodes;
            for (const auto &node : latest_->frame.observations) old_nodes.emplace(node.id, &node);
            std::unordered_set<UiNodeId> seen;
            std::vector<UiSemanticChange> delta;
            for (const auto &node : snapshot->frame.observations)
            {
                seen.insert(node.id);
                const auto old = old_nodes.find(node.id);
                if (old == old_nodes.end())
                    delta.push_back({UiChangeKind::Added, node.id, node.semantic_key, 0});
                else if (const std::uint16_t fields = ChangedFields(*old->second, node); fields != 0)
                    delta.push_back({UiChangeKind::Updated, node.id, node.semantic_key, fields});
            }
            std::unordered_map<std::string, const UiTheme *> old_styles;
            for (const auto &style : latest_->frame.styles)
                old_styles.emplace(style.id, &style.declared);
            std::unordered_set<std::string> changed_styles;
            for (const auto &style : snapshot->frame.styles)
            {
                const auto old = old_styles.find(style.id);
                if (old == old_styles.end() || *old->second != style.declared)
                    changed_styles.insert(style.id);
                old_styles.erase(style.id);
            }
            for (const auto &[removed_style, unused] : old_styles)
            { (void)unused; changed_styles.insert(removed_style); }
            for (const auto &node : snapshot->frame.observations)
                if (changed_styles.contains(node.style_id))
                {
                    const auto change = std::find_if(delta.begin(), delta.end(), [&node](const auto &item)
                    { return item.element_id == node.id && item.kind == UiChangeKind::Updated; });
                    if (change != delta.end()) change->fields |= Field(UiChangeField::Style);
                    else if (old_nodes.contains(node.id))
                        delta.push_back({UiChangeKind::Updated, node.id, node.semantic_key,
                                         Field(UiChangeField::Style)});
                }
            for (UiSemanticChange &change : delta)
                if (change.kind != UiChangeKind::Removed)
                {
                    const auto node = snapshot->node_indices.find(change.element_id);
                    if (node == snapshot->node_indices.end()) continue;
                    const UiNodeObservation &observation = snapshot->frame.observations[node->second];
                    change.current = MakeSemanticElement(observation);
                    if (const auto style = snapshot->style_indices.find(observation.style_id);
                        style != snapshot->style_indices.end())
                        change.style = snapshot->styles[style->second];
                }
            for (const auto &node : latest_->frame.observations)
                if (!seen.contains(node.id))
                    delta.push_back({UiChangeKind::Removed, node.id, node.semantic_key, 0});
            history_.emplace_back(latest_->frame.frame, snapshot->frame.frame, std::move(delta));
            while (history_.size() > kHistoryLimit) history_.pop_front();
        }
        else history_.clear();
        latest_ = std::move(snapshot);
    }

    UiSemanticView UiSemanticInterface::Query() const
    {
        std::scoped_lock lock(mutex_);
        return UiSemanticView{latest_};
    }

    UiSemanticChanges UiSemanticInterface::ChangesSince(
        const UiSessionId session, const std::uint64_t frame) const
    {
        std::scoped_lock lock(mutex_);
        UiSemanticChanges result{};
        if (!latest_) return result;
        result.session = latest_->frame.session;
        result.from_frame = frame;
        result.to_frame = latest_->frame.frame;
        if (session != result.session || frame > result.to_frame)
        { result.status = UiChangesStatus::FullSnapshotRequired; return result; }
        if (frame == result.to_frame) { result.status = UiChangesStatus::Ok; return result; }
        auto first = std::find_if(history_.begin(), history_.end(),
            [frame](const auto &entry) { return std::get<0>(entry) == frame; });
        if (first == history_.end())
        { result.status = UiChangesStatus::FullSnapshotRequired; return result; }
        result.status = UiChangesStatus::Ok;
        std::uint64_t cursor = frame;
        for (auto it = first; it != history_.end() && cursor < result.to_frame; ++it)
        {
            if (std::get<0>(*it) != cursor)
            { result.status = UiChangesStatus::FullSnapshotRequired; result.changes.clear(); return result; }
            cursor = std::get<1>(*it);
            result.changes.insert(result.changes.end(), std::get<2>(*it).begin(), std::get<2>(*it).end());
        }
        if (cursor != result.to_frame)
        { result.status = UiChangesStatus::FullSnapshotRequired; result.changes.clear(); }
        return result;
    }
}
