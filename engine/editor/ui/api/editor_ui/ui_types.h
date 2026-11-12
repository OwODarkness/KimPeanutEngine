#ifndef KPENGINE_EDITOR_UI_API_UI_TYPES_H
#define KPENGINE_EDITOR_UI_API_UI_TYPES_H

#include <cstdint>
#include <cstddef>
#include <span>
#include <optional>
#include <vector>
#include <string>
#include <string_view>
#include <variant>

namespace kpengine::editor::ui
{
    using UiSessionId = std::uint64_t;
    using UiNodeId = std::uint64_t;
    using UiActionId = std::uint64_t;
    using UiImageId = std::uint64_t;

    inline UiNodeId StableUiNodeId(const std::string_view name) noexcept
    {
        constexpr UiNodeId offset = 14695981039346656037ULL;
        constexpr UiNodeId prime = 1099511628211ULL;
        UiNodeId id = offset;
        for (const unsigned char byte : name)
        {
            id ^= byte;
            id *= prime;
        }
        return id == 0 ? 1 : id;
    }

    inline UiImageId StableUiImageId(const std::string_view name) noexcept
    {
        return StableUiNodeId(name);
    }

    struct UiVec2 { float x = 0.0F; float y = 0.0F; };
    struct UiRect { UiVec2 min{}; UiVec2 max{}; };
    struct UiColor
    {
        float r = 0.0F;
        float g = 0.0F;
        float b = 0.0F;
        float a = 1.0F;
        bool operator==(const UiColor &) const noexcept = default;
    };
    struct UiViewportInfo
    {
        UiVec2 logical_extent{};
        UiVec2 framebuffer_scale{1.0F, 1.0F};
        UiVec2 logical_origin{};
    };

    enum class UiRole : std::uint8_t
    {
        Generic, Button, CheckBox, Selectable, TextInput, NumericInput, Slider, Combo,
        Window, Group, Image, Canvas, Session, Panel, TabBar, Tab, Table, Row, Heading,
        Text, VirtualList, Splitter, Chart
    };
    enum class UiGeometryProvenance : std::uint8_t
    { Unavailable, TestFixture, ActualBackend, DerivedFromChildren };
    enum class UiActionKind : std::uint8_t { Activate, Toggle, Select, Edit, Commit, Cancel, Close };
    using UiValue = std::variant<bool, std::int64_t, std::uint64_t, double, std::string>;
    struct UiChoice { std::string key; std::string label; UiValue value{false}; };

    enum class UiTextRole : std::uint8_t { Primary, Muted, Accent, Warning, Error, Success };

    struct UiIconMask
    {
        std::span<const std::uint8_t> alpha;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        bool IsValid() const noexcept
        {
            return width > 0 && height > 0 && alpha.size() / width >= height;
        }
    };

    struct UiTableColumn
    {
        std::string_view label;
        // Positive widths are fixed logical pixels; zero lets the backend stretch the column.
        float width = 0.0F;
    };

    enum class UiPlotScale : std::uint8_t { Linear, Log10 };
    enum class UiPlotSeriesKind : std::uint8_t { Line, Bars, Points };
    struct UiPlotPoint { double x = 0.0; double y = 0.0; };

    struct UiChartAxisObservation
    {
        std::string label;
        std::string unit;
        double minimum = 0.0;
        double maximum = 1.0;
        UiPlotScale scale = UiPlotScale::Linear;
    };

    struct UiChartSeriesObservation
    {
        std::string label;
        UiPlotSeriesKind kind = UiPlotSeriesKind::Line;
        float emission_strength = 0.0F;
        std::size_t point_count = 0;
        std::vector<UiPlotPoint> points;
    };

    struct UiChartObservation
    {
        UiChartAxisObservation x;
        UiChartAxisObservation y;
        std::vector<UiChartSeriesObservation> series;
        std::string empty_label;
        bool truncated = false;
    };

    struct UiListRange
    {
        std::size_t first = 0;
        std::size_t last = 0;
    };

    struct UiControlDescriptor
    {
        std::string_view id;
        std::string_view label;
        std::string_view description;
        bool enabled = true;
        std::string_view disabled_reason;
        std::string_view hint;
        std::string_view unit;
        std::string_view domain_summary;
        std::optional<UiValue> semantic_value;
        std::optional<UiValue> semantic_minimum;
        std::optional<UiValue> semantic_maximum;
        std::vector<UiActionKind> semantic_actions;
        float width = 0.0F;
        float height = 0.0F;
        bool multiline = false;
        bool fixed_geometry = false;
        bool preserve_geometry = false;
        UiTextRole text_role = UiTextRole::Primary;
        bool emphasized = false;
        bool span_row = false;
        UiIconMask icon{};
        bool window_no_title_bar = false;
        bool window_no_background = false;
        bool window_no_border = false;
        bool window_no_collapse = false;
        bool window_no_saved_settings = false;
        bool window_no_bring_to_front = false;
        bool window_no_inputs = false;
        bool window_padding_zero = false;
        std::optional<UiRole> semantic_role;
        bool semantic_selected = false;
        struct SelectionDecoration
        {
            UiColor color{};
            float thickness = 1.0F;
            float emission_strength = 0.0F;
        };
        std::optional<SelectionDecoration> selection_decoration;
    };

    enum class UiCanvasPrimitiveKind : std::uint8_t
    {
        Line, FilledRect, Rect, Circle, Text, Image
    };

    struct UiCanvasTextRun
    {
        std::string text;
        UiColor color{};
    };

    struct UiCanvasPrimitive
    {
        UiCanvasPrimitiveKind kind = UiCanvasPrimitiveKind::Line;
        UiVec2 first{};
        UiVec2 second{};
        UiColor color{};
        float thickness = 1.0F;
        float radius = 0.0F;
        std::string text;
        std::vector<UiCanvasTextRun> text_runs;
        UiVec2 text_anchor{};
        float text_scale = 1.0F;
        UiImageId image = 0;
        // Backend-neutral request for the renderer's optional bloom pass.
        float emission_strength = 0.0F;
    };

    struct UiCanvas
    {
        UiControlDescriptor control{};
        UiVec2 size{};
        std::vector<UiCanvasPrimitive> primitives;
    };

    struct UiCanvasResult
    {
        bool hovered = false;
        bool pressed = false;
        bool dragging = false;
        UiVec2 normalized_pointer{};
        UiVec2 pointer_delta{};
        float wheel_delta = 0.0F;
        bool secondary_dragging = false;
        UiRect bounds{};
        UiGeometryProvenance geometry = UiGeometryProvenance::Unavailable;
        bool released = false;
        bool cancelled = false;
        UiVec2 pointer_position{};
    };

    struct UiEditResult
    {
        UiValue value{false};
        bool changed = false;
        bool committed = false;
        bool cancelled = false;
        bool activated = false;
        std::optional<UiActionKind> action;
    };

    template <typename T>
    struct UiTypedEditResult
    {
        T value{};
        bool changed = false;
        bool committed = false;
        bool cancelled = false;
        bool activated = false;
        std::optional<UiActionKind> action;
    };

    template <typename T>
    UiTypedEditResult<T> MakeTypedEditResult(UiEditResult result)
    {
        return {std::get<T>(result.value), result.changed, result.committed,
                result.cancelled, result.activated, result.action};
    }

    struct UiControl
    {
        UiNodeId id = 0;
        UiRole role = UiRole::Generic;
        std::string label;
        std::string description;
        bool enabled = true;
        std::string disabled_reason;
        std::string hint;
        std::string unit;
        std::string domain_summary;
        float width = 0.0F;
        UiValue value{false};
        std::optional<UiValue> minimum;
        std::optional<UiValue> maximum;
        std::optional<UiValue> step;
        std::vector<UiChoice> choices;
        std::vector<UiActionKind> actions;
        std::string style_id;
        float height = 0.0F;
        bool multiline = false;
        bool fixed_geometry = false;
        bool preserve_geometry = false;
        UiTextRole text_role = UiTextRole::Primary;
        bool emphasized = false;
        bool span_row = false;
        bool window_no_title_bar = false;
        bool window_no_background = false;
        bool window_no_border = false;
        bool window_no_collapse = false;
        bool window_no_saved_settings = false;
        bool window_no_bring_to_front = false;
        bool window_no_inputs = false;
        bool window_padding_zero = false;
        std::optional<UiControlDescriptor::SelectionDecoration> selection_decoration;
    };

    struct UiNodeObservation
    {
        UiSessionId session = 0;
        std::uint64_t frame = 0;
        UiNodeId id = 0;
        UiNodeId parent = 0;
        UiRole role = UiRole::Generic;
        std::string label;
        std::string description;
        // Absent means the role has no intrinsic value (for example Button).
        std::optional<UiValue> value;
        bool enabled = true;
        std::string disabled_reason;
        // These are backend observations. Empty means unsupported or unavailable.
        // focused is keyboard/navigation focus; active is a current interaction;
        // selected is role-specific selection and is never a checkbox's checked value.
        std::optional<bool> focused;
        std::optional<bool> hovered;
        std::optional<bool> clipped;
        UiRect bounds{};
        UiGeometryProvenance geometry = UiGeometryProvenance::Unavailable;
        std::optional<bool> active;
        std::vector<UiActionKind> actions;
        std::optional<UiActionKind> last_action;
        std::string unit;
        std::string domain_summary;
        std::optional<UiValue> minimum;
        std::optional<UiValue> maximum;
        std::optional<UiValue> step;
        std::vector<UiChoice> choices;
        std::string style_id;
        std::optional<bool> selected;
        std::optional<bool> expanded;
        std::size_t virtual_list_total = 0;
        std::size_t virtual_list_first = 0;
        std::size_t virtual_list_last = 0;
        std::string semantic_key;
        std::vector<std::string> table_columns;
        std::optional<std::size_t> table_column;
        std::optional<UiChartObservation> chart;
        std::optional<UiRect> clipped_bounds;
        // True only when emitted in this frame and intersecting its active clip.
        // Higher-window occlusion is not measured by the current backend.
        std::optional<bool> visible;
    };
}

#endif
