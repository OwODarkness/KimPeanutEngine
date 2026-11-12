#ifndef KPENGINE_EDITOR_UI_API_UI_SEMANTIC_INTERFACE_H
#define KPENGINE_EDITOR_UI_API_UI_SEMANTIC_INTERFACE_H

#include "ui_types.h"
#include "ui_theme.h"

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace kpengine::editor::ui
{
    using UiElementId = UiNodeId;
    struct UiFrameResult;

    struct UiSemanticTreeNode
    {
        UiNodeId id = 0;
        UiNodeId parent = 0;
        UiRole role = UiRole::Generic;
        std::string label;
        UiRect bounds{};
        UiGeometryProvenance geometry = UiGeometryProvenance::Unavailable;
        std::optional<UiRect> clipped_bounds;
        bool enabled = true;
        std::optional<bool> selected, active, focused, hovered, clipped, visible;
        std::vector<UiNodeId> children;
    };

    struct UiSemanticTree
    {
        UiSessionId session = 0;
        std::uint64_t frame = 0;
        bool complete = false;
        UiViewportInfo viewport{};
        std::vector<UiNodeId> roots;
        std::vector<UiSemanticTreeNode> nodes;
    };

    struct UiElementInfo
    {
        const UiNodeObservation *element = nullptr;
        std::span<const UiNodeId> children;
    };

    struct UiElementStyle
    {
        std::string id;
        UiTheme declared{};
        bool has_resolved_values = false;
    };

    enum class UiQueryLevel : std::uint8_t { Semantic, Spatial, Visual };
    enum class UiQueryStatus : std::uint8_t
    { Ok, NoSnapshot, NotFound, FilteredOut, Invisible, Unavailable };

    struct UiSemanticQuery
    {
        UiQueryLevel level = UiQueryLevel::Semantic;
        std::optional<UiNodeId> element_id;
        std::optional<std::string> semantic_id;
        std::optional<UiNodeId> root_id;
        std::optional<UiRole> role;
        std::optional<bool> enabled, selected, active, focused, visible;
        bool descendants = true;
    };

    struct UiSemanticElement
    {
        UiNodeId element_id = 0;
        UiNodeId parent_id = 0;
        std::string semantic_id;
        UiRole role = UiRole::Generic;
        std::string title;
        std::string description;
        std::optional<UiValue> value;
        bool enabled = true;
        std::string disabled_reason;
        std::optional<bool> selected, active, focused, hovered, visible, clipped, expanded;
        std::vector<UiActionKind> actions;
        std::optional<UiActionKind> last_action;
        std::string unit, domain_summary;
        std::optional<UiValue> minimum, maximum, step;
        std::vector<UiChoice> choices;
        std::vector<std::string> table_columns;
        std::optional<std::size_t> table_column;
        std::optional<UiChartObservation> chart;
        std::optional<UiRect> bounds, clipped_bounds;
        UiGeometryProvenance geometry = UiGeometryProvenance::Unavailable;
        std::optional<std::string> style_id;
    };

    struct UiSemanticQueryResult
    {
        UiQueryStatus status = UiQueryStatus::NoSnapshot;
        UiSessionId session = 0;
        std::uint64_t frame = 0;
        UiQueryLevel level = UiQueryLevel::Semantic;
        std::vector<UiSemanticElement> elements;
        std::vector<UiElementStyle> styles;
        std::optional<UiViewportInfo> viewport;
    };

    enum class UiChangeKind : std::uint8_t { Added, Removed, Updated };
    enum class UiChangeField : std::uint16_t
    {
        Structure = 1, Semantics = 2, State = 4, Value = 8, Geometry = 16,
        Style = 32, Actions = 64, Constraints = 128, Table = 256, Chart = 512
    };
    constexpr std::uint16_t operator|(UiChangeField lhs, UiChangeField rhs) noexcept
    { return static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs); }
    struct UiSemanticChange
    {
        UiChangeKind kind = UiChangeKind::Updated;
        UiNodeId element_id = 0;
        std::string semantic_id;
        std::uint16_t fields = 0;
        std::optional<UiSemanticElement> current;
        std::optional<UiElementStyle> style;
    };
    enum class UiChangesStatus : std::uint8_t { Ok, NoSnapshot, FullSnapshotRequired };
    struct UiSemanticChanges
    {
        UiChangesStatus status = UiChangesStatus::NoSnapshot;
        UiSessionId session = 0;
        std::uint64_t from_frame = 0;
        std::uint64_t to_frame = 0;
        std::vector<UiSemanticChange> changes;
    };

    struct UiSemanticSnapshot;
    class UiSemanticView final
    {
    public:
        bool IsValid() const noexcept;
        UiSessionId SessionId() const noexcept;
        std::uint64_t FrameId() const noexcept;
        const UiFrameResult &FrameResult() const noexcept;
        const UiSemanticTree &GetUITree() const noexcept;
        std::optional<UiElementInfo> GetElementInfo(UiElementId id) const;
        const UiElementStyle *GetElementStyle(UiElementId id) const noexcept;
        UiSemanticQueryResult Query(const UiSemanticQuery &query = {}) const;

    private:
        friend class UiSemanticInterface;
        explicit UiSemanticView(std::shared_ptr<const UiSemanticSnapshot> snapshot)
            : snapshot_(std::move(snapshot)) {}
        std::shared_ptr<const UiSemanticSnapshot> snapshot_;
    };

    class UiSemanticInterface final
    {
    public:
        void Update(const UiFrameResult &frame);
        UiSemanticView Query() const;
        UiSemanticChanges ChangesSince(UiSessionId session, std::uint64_t frame) const;

    private:
        mutable std::mutex mutex_;
        std::shared_ptr<const UiSemanticSnapshot> latest_;
        std::deque<std::tuple<std::uint64_t, std::uint64_t, std::vector<UiSemanticChange>>> history_;
        static constexpr std::size_t kHistoryLimit = 32;
    };
}

#endif
