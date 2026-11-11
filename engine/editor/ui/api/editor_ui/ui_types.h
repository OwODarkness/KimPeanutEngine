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
    struct UiColor { float r = 0.0F; float g = 0.0F; float b = 0.0F; float a = 1.0F; };
    struct UiViewportInfo
    {
        UiVec2 logical_extent{};
        UiVec2 framebuffer_scale{1.0F, 1.0F};
        UiVec2 logical_origin{};
    };

    enum class UiRole : std::uint8_t
    {
        Generic, Button, CheckBox, Selectable, TextInput, NumericInput, Slider, Combo,
        Window, Group, Image, Canvas
    };
    enum class UiGeometryProvenance : std::uint8_t { Unavailable, TestFixture, ActualBackend };
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
    };

    enum class UiCanvasPrimitiveKind : std::uint8_t
    {
        Line, FilledRect, Rect, Circle, Text, Image
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
        UiVec2 text_anchor{};
        float text_scale = 1.0F;
        UiImageId image = 0;
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
        float width = 0.0F;
        UiValue value{false};
        std::optional<UiValue> minimum;
        std::optional<UiValue> maximum;
        std::optional<UiValue> step;
        std::vector<UiChoice> choices;
        std::vector<UiActionKind> actions;
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
        UiValue value{false};
        bool enabled = true;
        std::string disabled_reason;
        bool focused = false;
        bool hovered = false;
        bool clipped = false;
        UiRect bounds{};
        UiGeometryProvenance geometry = UiGeometryProvenance::Unavailable;
        bool active = false;
        std::vector<UiActionKind> actions;
        std::optional<UiActionKind> last_action;
    };
}

#endif
