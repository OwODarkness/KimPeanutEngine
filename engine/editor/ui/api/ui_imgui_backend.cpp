#include "ui_imgui_inline_backend_internal.h"

#include <imgui.h>

#include <stdexcept>
#include <bit>
#include <cstdint>
#include <type_traits>
#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace kpengine::editor::ui
{
    namespace
    {
        ImGuiDataType DataType(const UiValue &value)
        {
            if (std::holds_alternative<std::int64_t>(value)) return ImGuiDataType_S64;
            if (std::holds_alternative<std::uint64_t>(value)) return ImGuiDataType_U64;
            if (std::holds_alternative<double>(value)) return ImGuiDataType_Double;
            throw std::invalid_argument("value is not numeric");
        }

        float WidgetSpeed(const std::optional<UiValue> &step)
        {
            if (!step.has_value()) return 0.0F;
            return std::visit([](const auto &value) -> float
            {
                using ValueType = std::decay_t<decltype(value)>;
                if constexpr (std::is_arithmetic_v<ValueType>)
                    return static_cast<float>(value);
                return 0.0F;
            }, *step);
        }

        ImVec4 Color(const UiColor color)
        {
            return {color.r, color.g, color.b, color.a};
        }

        struct ImGuiInlineBackend final : IUiBackend
        {
            explicit ImGuiInlineBackend(ImFont *code_font,
                                        UiImageDrawCallback draw_image) noexcept
                : code_font_(code_font), draw_image_(std::move(draw_image)) {}

            bool Initialize(const UiViewportInfo &) override
            {
                return ImGui::GetCurrentContext() != nullptr;
            }

            void BeginFrame(const UiFrameInfo &frame) override
            {
                if (ImGui::GetCurrentContext() == nullptr)
                    throw std::logic_error("inline ImGui backend requires an active host context");
                session_ = frame.session;
                frame_ = frame.frame;
                observe_ = frame.observe;
                viewport_ = frame.viewport;
                observations_.clear();
                window_bounds_.clear();
                window_id_ = 0;
            }

            void ApplyWindowGeometry(const UiNodeId id, const UiVec2 position,
                                     const UiVec2 size, const bool fixed_geometry,
                                     const bool preserve_geometry)
            {
                const auto previous = window_viewports_.find(id);
                const bool viewport_changed = previous != window_viewports_.end() &&
                    (previous->second.logical_extent.x != viewport_.logical_extent.x ||
                     previous->second.logical_extent.y != viewport_.logical_extent.y ||
                     previous->second.logical_origin.x != viewport_.logical_origin.x ||
                     previous->second.logical_origin.y != viewport_.logical_origin.y ||
                     previous->second.framebuffer_scale.x != viewport_.framebuffer_scale.x ||
                     previous->second.framebuffer_scale.y != viewport_.framebuffer_scale.y);
                const ImGuiCond condition = fixed_geometry ? ImGuiCond_Always :
                    preserve_geometry ? ImGuiCond_FirstUseEver :
                    previous == window_viewports_.end() || viewport_changed
                        ? ImGuiCond_Always : ImGuiCond_None;
                if (condition != ImGuiCond_None)
                {
                    ImGui::SetNextWindowPos({position.x, position.y}, condition);
                    ImGui::SetNextWindowSize({size.x, size.y}, condition);
                }
                window_viewports_.insert_or_assign(id, viewport_);
            }

            bool BeginWindow(const UiControl &window) override
            {
                return DrawWindow(window, nullptr);
            }

            bool BeginWindow(const UiControl &window, bool &open,
                             const UiVec2 position, const UiVec2 size) override
            {
                if (size.x > 0.0F && size.y > 0.0F)
                    ApplyWindowGeometry(window.id, position, size, window.fixed_geometry,
                                        window.preserve_geometry);
                return DrawWindow(window, &open);
            }

            bool BeginWindow(const UiControl &window, const UiVec2 position,
                             const UiVec2 size) override
            {
                if (size.x > 0.0F && size.y > 0.0F)
                    ApplyWindowGeometry(window.id, position, size, window.fixed_geometry,
                                        window.preserve_geometry);
                return DrawWindow(window, nullptr);
            }

            bool DrawWindow(const UiControl &window, bool *open)
            {
                window_id_ = window.id;
                char suffix[24]{};
                std::snprintf(suffix, sizeof(suffix), "###ui_%016llx",
                              static_cast<unsigned long long>(window.id));
                const std::string internal_name = window.label + suffix;
                ImGuiWindowFlags flags = window.fixed_geometry
                    ? ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                    : ImGuiWindowFlags_None;
                if (window.window_no_title_bar) flags |= ImGuiWindowFlags_NoTitleBar;
                if (window.window_no_background) flags |= ImGuiWindowFlags_NoBackground;
                if (window.window_no_collapse) flags |= ImGuiWindowFlags_NoCollapse;
                if (window.window_no_saved_settings) flags |= ImGuiWindowFlags_NoSavedSettings;
                if (window.window_no_bring_to_front) flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
                if (window.window_no_inputs) flags |= ImGuiWindowFlags_NoInputs;
                if (window.window_padding_zero) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                if (window.window_no_border) ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
                const bool visible = ImGui::Begin(internal_name.c_str(), open, flags);
                window_padding_open_ = window.window_padding_zero;
                window_border_open_ = window.window_no_border;
                const ImVec2 position = ImGui::GetWindowPos();
                const ImVec2 size = ImGui::GetWindowSize();
                window_bounds_.insert_or_assign(window.id,
                    UiRect{{position.x, position.y}, {position.x + size.x, position.y + size.y}});
                if (!observe_) return visible;
                observations_.push_back({session_, frame_, window.id, 0, window.role,
                    window.label, window.description, window.value, window.enabled,
                    window.disabled_reason, ImGui::IsWindowFocused(),
                    ImGui::IsWindowHovered(), false,
                    {{position.x, position.y}, {position.x + size.x, position.y + size.y}},
                    UiGeometryProvenance::ActualBackend, false});
                return visible;
            }

            std::optional<UiRect> WindowBounds(const UiNodeId id) const noexcept override
            {
                const auto found = window_bounds_.find(id);
                return found == window_bounds_.end() ? std::nullopt :
                    std::optional<UiRect>{found->second};
            }

            UiVec2 PointerPosition() const noexcept override
            {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                return {mouse.x, mouse.y};
            }

            void EndWindow() override
            {
                ImGui::End();
                if (window_border_open_) ImGui::PopStyleVar();
                if (window_padding_open_) ImGui::PopStyleVar();
                window_border_open_ = false;
                window_padding_open_ = false;
                window_id_ = 0;
            }

            static void PushUiId(const UiNodeId id)
            {
                const auto bits = static_cast<std::uint64_t>(id);
                ImGui::PushID(std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(bits >> 32U)));
                ImGui::PushID(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits)));
            }

            static void PopUiId() { ImGui::PopID(); ImGui::PopID(); }

            bool BeginTabBar(const UiControl &control) override
            {
                PushUiId(control.id);
                if (ImGui::BeginTabBar(control.label.c_str())) return true;
                PopUiId();
                return false;
            }

            void EndTabBar() override
            {
                ImGui::EndTabBar();
                PopUiId();
            }

            bool BeginTabItem(const UiControl &control) override
            {
                PushUiId(control.id);
                if (ImGui::BeginTabItem(control.label.c_str())) return true;
                PopUiId();
                return false;
            }

            void EndTabItem() override
            {
                ImGui::EndTabItem();
                PopUiId();
            }

            void PushTheme(const UiTheme &theme) override
            {
                theme_stack_.push_back(theme);
                if (theme.font == UiFontRole::Code && code_font_ != nullptr)
                    ImGui::PushFont(code_font_);
                ImGui::PushStyleColor(ImGuiCol_Text, Color(theme.text));
                ImGui::PushStyleColor(ImGuiCol_TextDisabled, Color(theme.muted));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(theme.surface));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, Color(theme.surface));
                ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(theme.panel));
                ImGui::PushStyleColor(ImGuiCol_Border, Color(theme.border));
                ImGui::PushStyleColor(ImGuiCol_Separator, Color(theme.border));
                ImGui::PushStyleColor(ImGuiCol_TitleBg, Color(theme.surface));
                ImGui::PushStyleColor(ImGuiCol_TitleBgActive, Color(theme.panel));
                ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, Color(theme.surface));
                ImGui::PushStyleColor(ImGuiCol_Header, Color(theme.raised));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(theme.border));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(theme.selected));
                ImGui::PushStyleColor(ImGuiCol_Button, Color(theme.raised));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(theme.border));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(theme.selected));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, Color(theme.panel));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Color(theme.raised));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Color(theme.border));
                ImGui::PushStyleColor(ImGuiCol_CheckMark, Color(theme.accent));
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, Color(theme.accent));
                ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, Color(theme.accent_active));
                ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, Color(theme.raised));
                ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, Color(theme.panel));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                    {theme.window_padding_x, theme.window_padding_y});
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                    {theme.frame_padding_x, theme.frame_padding_y});
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                    {theme.item_spacing_x, theme.item_spacing_y});
                ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,
                    {theme.inner_spacing_x, theme.inner_spacing_y});
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.window_rounding);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.frame_rounding);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, theme.window_border_size);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, theme.frame_border_size);
            }

            void PopTheme() override
            {
                if (theme_stack_.empty())
                    throw std::logic_error("ImGui UI theme stack underflow");
                ImGui::PopStyleVar(8);
                ImGui::PopStyleColor(24);
                if (theme_stack_.back().font == UiFontRole::Code && code_font_ != nullptr)
                    ImGui::PopFont();
                theme_stack_.pop_back();
            }

            void SetHostContainer(const UiControl &container) override
            {
                window_id_ = container.id;
                if (!observe_) return;
                const ImVec2 position = ImGui::GetWindowPos();
                const ImVec2 size = ImGui::GetWindowSize();
                observations_.push_back({session_, frame_, container.id, 0, container.role,
                    container.label, container.description, container.value, container.enabled,
                    container.disabled_reason, ImGui::IsWindowFocused(),
                    ImGui::IsWindowHovered(), false,
                    {{position.x, position.y}, {position.x + size.x, position.y + size.y}},
                    UiGeometryProvenance::ActualBackend, false});
            }

            UiEditResult Control(const UiControl &control) override
            {
                return DrawControl(control, {});
            }

            UiEditResult IconControl(const UiControl &control, const UiIconMask icon) override
            {
                return DrawControl(control, icon);
            }

            UiEditResult DrawControl(const UiControl &control, const UiIconMask icon)
            {
                UiEditResult result{control.value, false, false, false};
                const auto id_bits = static_cast<std::uint64_t>(control.id);
                ImGui::PushID(std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(id_bits >> 32U)));
                ImGui::PushID(std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(id_bits)));
                if (!control.enabled) ImGui::BeginDisabled();
                const bool tinted = control.text_role != UiTextRole::Primary && !theme_stack_.empty();
                if (tinted) ImGui::PushStyleColor(ImGuiCol_Text, Color(TextColor(control.text_role)));
                switch (control.role)
                {
                case UiRole::Button:
                {
                    const bool emphasized = control.emphasized && !theme_stack_.empty();
                    if (emphasized)
                    {
                        const auto &theme = theme_stack_.back();
                        ImGui::PushStyleColor(ImGuiCol_Button, Color(theme.selected));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(theme.selected_hovered));
                        ImGui::PushStyleColor(ImGuiCol_Border, Color(theme.accent_active));
                    }
                    const bool has_icon = icon.IsValid();
                    result.activated = ImGui::Button(has_icon ? "##icon" : control.label.c_str(),
                                                     {control.width, control.height});
                    if (has_icon)
                    {
                        const ImVec2 min = ImGui::GetItemRectMin();
                        const ImVec2 max = ImGui::GetItemRectMax();
                        const float size = std::min({18.0F, max.x - min.x, max.y - min.y});
                        DrawIcon(icon, {(min.x + max.x - size) * 0.5F,
                                        (min.y + max.y - size) * 0.5F}, size);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("%s", control.label.c_str());
                    }
                    if (emphasized) ImGui::PopStyleColor(3);
                    if (result.activated) result.action = UiActionKind::Activate;
                    break;
                }
                case UiRole::CheckBox:
                {
                    bool value = std::get<bool>(control.value);
                    result.changed = ImGui::Checkbox(control.label.c_str(), &value);
                    result.value = value;
                    result.committed = result.changed;
                    if (result.changed) result.action = UiActionKind::Toggle;
                    break;
                }
                case UiRole::Selectable:
                {
                    const bool selected = std::get<bool>(control.value);
                    const bool themed = selected && !theme_stack_.empty();
                    if (themed)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Header, Color(theme_stack_.back().selected));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                              Color(theme_stack_.back().selected_hovered));
                    }
                    const ImGuiSelectableFlags flags = control.span_row
                        ? ImGuiSelectableFlags_SpanAllColumns : ImGuiSelectableFlags_None;
                    const bool has_icon = icon.IsValid();
                    const std::string label = has_icon ? "   " + control.label : control.label;
                    const ImVec2 cursor = ImGui::GetCursorScreenPos();
                    result.activated = ImGui::Selectable(label.c_str(), selected, flags,
                                                         {control.width, control.height});
                    if (has_icon && ImGui::IsItemVisible())
                    {
                        const float size = ImGui::GetFontSize();
                        DrawIcon(icon, cursor, size);
                    }
                    if (themed) ImGui::PopStyleColor(2);
                    result.value = result.activated ? UiValue{true} : control.value;
                    result.changed = result.activated && !selected;
                    result.committed = result.activated;
                    if (result.activated) result.action = UiActionKind::Select;
                    break;
                }
                case UiRole::NumericInput:
                case UiRole::Slider:
                {
                    if (control.width > 0.0F) ImGui::SetNextItemWidth(control.width);
                    UiValue value = control.value;
                    result.changed = std::visit(
                        [&control](auto &typed)
                        {
                            using ValueType = std::decay_t<decltype(typed)>;
                            if constexpr (std::is_arithmetic_v<ValueType> &&
                                          !std::is_same_v<ValueType, bool>)
                            {
                                const auto *minimum = control.minimum.has_value()
                                                          ? std::get_if<ValueType>(&*control.minimum)
                                                          : nullptr;
                                const auto *maximum = control.maximum.has_value()
                                                          ? std::get_if<ValueType>(&*control.maximum)
                                                          : nullptr;
                                if (control.role == UiRole::Slider)
                                    return ImGui::SliderScalar(control.label.c_str(),
                                        DataType(control.value), &typed, minimum, maximum);
                                return ImGui::DragScalar(control.label.c_str(), DataType(control.value),
                                    &typed, WidgetSpeed(control.step), minimum, maximum);
                            }
                            else
                                return false;
                        }, value);
                    result.value = std::move(value);
                    result.committed = ImGui::IsItemDeactivatedAfterEdit();
                    if (result.committed) result.action = UiActionKind::Commit;
                    else if (result.changed) result.action = UiActionKind::Edit;
                    break;
                }
                case UiRole::TextInput:
                {
                    if (control.width > 0.0F) ImGui::SetNextItemWidth(control.width);
                    const std::string &source = std::get<std::string>(control.value);
                    auto [buffer, inserted] = text_buffers_.try_emplace(control.id, source);
                    const bool was_active = text_active_[control.id];
                    if (!inserted && !was_active && buffer->second != source)
                        buffer->second = source;
                    buffer->second.resize(4096, '\0');
                    if (control.multiline)
                    {
                        (void)ImGui::InputTextMultiline(control.label.c_str(),
                            buffer->second.data(), buffer->second.size(),
                            ImVec2(control.width, control.height));
                    }
                    else
                    {
                        (void)ImGui::InputTextWithHint(control.label.c_str(), control.hint.c_str(),
                            buffer->second.data(), buffer->second.size());
                    }
                    const std::size_t text_length = buffer->second.find('\0');
                    if (text_length != std::string::npos)
                        buffer->second.resize(text_length);
                    text_active_[control.id] = ImGui::IsItemActive();
                    result.changed = buffer->second != source;
                    result.cancelled = ImGui::IsItemFocused() &&
                                       ImGui::IsKeyPressed(ImGuiKey_Escape);
                    if (result.cancelled)
                    {
                        buffer->second = source;
                        result.value = source;
                        result.changed = false;
                        result.action = UiActionKind::Cancel;
                    }
                    else
                    {
                        result.value = buffer->second;
                        result.committed = ImGui::IsItemDeactivatedAfterEdit();
                        if (result.committed) result.action = UiActionKind::Commit;
                        else if (result.changed) result.action = UiActionKind::Edit;
                    }
                    break;
                }
                case UiRole::Combo:
                {
                    const auto selected = std::find_if(
                        control.choices.begin(), control.choices.end(),
                        [&control](const UiChoice &choice) { return choice.value == control.value; });
                    if (control.width > 0.0F) ImGui::SetNextItemWidth(control.width);
                    const char *preview = selected == control.choices.end()
                                              ? "Unavailable"
                                              : selected->label.c_str();
                    if (control.height > 0.0F)
                    {
                        const ImVec2 padding = ImGui::GetStyle().FramePadding;
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            {padding.x, std::max(padding.y, (control.height - ImGui::GetFontSize()) * 0.5F)});
                    }
                    const bool open = ImGui::BeginCombo(control.label.c_str(), preview);
                    if (control.height > 0.0F) ImGui::PopStyleVar();
                    if (open)
                    {
                        for (const UiChoice &choice : control.choices)
                        {
                            const bool is_selected = choice.value == control.value;
                            if (ImGui::Selectable(choice.label.c_str(), is_selected))
                            {
                                result.value = choice.value;
                                result.changed = result.committed = true;
                                result.activated = true;
                                result.action = UiActionKind::Select;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    break;
                }
                default:
                    ImGui::TextDisabled("Unsupported neutral control: %s", control.label.c_str());
                    break;
                }
                if (tinted) ImGui::PopStyleColor();
                if (!control.enabled) ImGui::EndDisabled();
                if (!control.enabled && !control.disabled_reason.empty() && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", control.disabled_reason.c_str());
                ImGui::PopID();
                ImGui::PopID();
                if (observe_)
                {
                    UiControl observed = control;
                    observed.value = result.value;
                    Observe(observed, window_id_, result.action);
                }
                return result;
            }

            UiCanvasResult Canvas(const UiControl &control, const UiVec2 requested_size,
                                  const std::span<const UiCanvasPrimitive> primitives) override
            {
                const ImVec2 size{requested_size.x, requested_size.y};
                ImGui::PushID(std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(control.id >> 32U)));
                ImGui::PushID(std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(control.id)));
                if (!control.enabled) ImGui::BeginDisabled();
                ImGui::InvisibleButton("##canvas", size);
                if (!control.enabled) ImGui::EndDisabled();
                const ImVec2 minimum = ImGui::GetItemRectMin();
                const ImVec2 maximum = ImGui::GetItemRectMax();
                UiCanvasResult result{};
                result.hovered = ImGui::IsItemHovered();
                result.pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                const bool item_active = ImGui::IsItemActive();
                const bool item_focused = ImGui::IsItemFocused();
                const bool item_visible = ImGui::IsItemVisible();
                result.dragging = item_active && ImGui::IsMouseDown(ImGuiMouseButton_Left);
                const ImGuiIO &io = ImGui::GetIO();
                result.released = active_canvas_id_ == control.id &&
                                  ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                result.cancelled = active_canvas_id_ == control.id &&
                    (io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                     (!io.MouseDown[ImGuiMouseButton_Left] && !result.released));
                if (result.pressed || result.dragging) active_canvas_id_ = control.id;
                if (result.released || result.cancelled) active_canvas_id_ = 0;
                result.secondary_dragging = result.hovered &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Right);
                ImDrawList *const draw = ImGui::GetWindowDrawList();
                const float width = maximum.x - minimum.x;
                const float height = maximum.y - minimum.y;
                result.bounds = {{minimum.x, minimum.y}, {maximum.x, maximum.y}};
                result.geometry = UiGeometryProvenance::ActualBackend;
                result.pointer_position = {ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y};
                if (result.hovered || result.dragging || result.secondary_dragging ||
                    active_canvas_id_ == control.id)
                {
                    const ImGuiIO &io = ImGui::GetIO();
                    result.pointer_delta = {io.MouseDelta.x, io.MouseDelta.y};
                    result.wheel_delta = io.MouseWheel;
                }
                if ((result.hovered || result.dragging) && width > 0.0F && height > 0.0F)
                {
                    const ImVec2 mouse = ImGui::GetIO().MousePos;
                    result.normalized_pointer = {
                        std::clamp((mouse.x - minimum.x) / width, 0.0F, 1.0F),
                        std::clamp((mouse.y - minimum.y) / height, 0.0F, 1.0F)};
                }
                draw->PushClipRect(minimum, maximum, true);
                const auto point = [minimum, width, height](const UiVec2 value)
                {
                    return ImVec2{minimum.x + value.x * width, minimum.y + value.y * height};
                };
                for (const UiCanvasPrimitive &primitive : primitives)
                {
                    const ImU32 color = ImGui::ColorConvertFloat4ToU32(
                        {primitive.color.r, primitive.color.g,
                         primitive.color.b, primitive.color.a});
                    const ImVec2 first = point(primitive.first);
                    const ImVec2 second = point(primitive.second);
                    switch (primitive.kind)
                    {
                    case UiCanvasPrimitiveKind::Line:
                        draw->AddLine(first, second, color, primitive.thickness);
                        break;
                    case UiCanvasPrimitiveKind::FilledRect:
                        draw->AddRectFilled(first, second, color);
                        break;
                    case UiCanvasPrimitiveKind::Rect:
                        draw->AddRect(first, second, color, 0.0F, 0, primitive.thickness);
                        break;
                    case UiCanvasPrimitiveKind::Circle:
                        draw->AddCircle(first, primitive.radius * std::min(width, height),
                                        color, 24, primitive.thickness);
                        break;
                    case UiCanvasPrimitiveKind::Text:
                    {
                        ImFont *const font = ImGui::GetFont();
                        const float size = ImGui::GetFontSize() * primitive.text_scale;
                        const char *const begin = primitive.text.data();
                        const char *const end = begin + primitive.text.size();
                        const ImVec2 extent = font->CalcTextSizeA(size, 1.0e6F, 0.0F, begin, end);
                        draw->AddText(font, size,
                            {first.x - extent.x * primitive.text_anchor.x,
                             first.y - extent.y * primitive.text_anchor.y}, color, begin, end);
                        break;
                    }
                    case UiCanvasPrimitiveKind::Image:
                    {
                        const UiRect image_bounds{{first.x, first.y}, {second.x, second.y}};
                        if (draw_image_) draw_image_(primitive.image, image_bounds, draw);
                        break;
                    }
                    }
                }
                if (observe_)
                {
                    observations_.push_back({session_, frame_, control.id, window_id_,
                        control.role, control.label, control.description, control.value,
                        control.enabled, control.disabled_reason, item_focused,
                        result.hovered, !item_visible,
                        {{minimum.x, minimum.y}, {maximum.x, maximum.y}},
                        UiGeometryProvenance::ActualBackend, item_active});
                }
                draw->PopClipRect();
                ImGui::PopID();
                ImGui::PopID();
                return result;
            }

            void Text(const std::string_view text) override
            {
                ImGui::TextUnformatted(text.data(), text.data() + text.size());
            }

            void SameLine() override { ImGui::SameLine(); }

            UiVec2 AvailableContent() const noexcept override
            {
                const ImVec2 available = ImGui::GetContentRegionAvail();
                return {available.x, available.y};
            }

            void Section(const std::string_view text) override
            {
                const std::string label{text};
                ImGui::PushStyleColor(ImGuiCol_Text, Color(TextColor(UiTextRole::Accent)));
                ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, {0.0F, 1.0F});
                ImGui::SeparatorText(label.c_str());
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }

            void WrappedText(const std::string_view text) override
            {
                ImGui::PushTextWrapPos(0.0F);
                Text(text);
                ImGui::PopTextWrapPos();
            }

            bool BeginTable(const UiNodeId id, const std::span<const UiTableColumn> columns,
                            const float height, const bool headers) override
            {
                std::array<std::string, 32> labels;
                for (std::size_t index = 0; index < columns.size(); ++index)
                    labels[index] = columns[index].label;
                PushUiId(id);
                ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp;
                if (height > 0.0F)
                    flags |= ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_BordersInnerH;
                if (!ImGui::BeginTable("##table", static_cast<int>(columns.size()), flags,
                                       {0.0F, height}))
                {
                    PopUiId();
                    return false;
                }
                for (std::size_t index = 0; index < columns.size(); ++index)
                {
                    const float width = columns[index].width;
                    ImGui::TableSetupColumn(labels[index].c_str(),
                        width > 0.0F ? ImGuiTableColumnFlags_WidthFixed
                                     : ImGuiTableColumnFlags_WidthStretch, width);
                }
                if (headers)
                {
                    if (height > 0.0F) ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableHeadersRow();
                }
                return true;
            }

            void EndTable() override
            {
                ImGui::EndTable();
                PopUiId();
            }

            void TableRow(const bool highlighted) override
            {
                ImGui::TableNextRow();
                if (highlighted && !theme_stack_.empty())
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                        ImGui::ColorConvertFloat4ToU32(Color(theme_stack_.back().selected)));
            }

            void TableColumn() override { ImGui::TableNextColumn(); }

            UiListRange BeginVirtualList(const UiNodeId id, const std::size_t item_count,
                                         const float row_height,
                                         const bool scroll_to_end) override
            {
                if (virtual_list_open_)
                    throw std::logic_error("ImGui UI backend virtual lists cannot be nested");
                PushUiId(id);
                virtual_list_open_ = true;
                const bool visible = ImGui::BeginChild(
                    "##virtual-list", {0.0F, 0.0F}, false,
                    ImGuiWindowFlags_HorizontalScrollbar);
                if (!visible || item_count == 0)
                    return {};
                if (scroll_to_end)
                    ImGui::SetScrollY(ImGui::GetScrollMaxY());
                const std::size_t bounded_count = std::min(
                    item_count, static_cast<std::size_t>(std::numeric_limits<int>::max()));
                list_clipper_.Begin(static_cast<int>(bounded_count), row_height);
                list_clipper_started_ = true;
                if (!list_clipper_.Step())
                    return {};
                return {static_cast<std::size_t>(list_clipper_.DisplayStart),
                        static_cast<std::size_t>(list_clipper_.DisplayEnd)};
            }

            void EndVirtualList() override
            {
                if (!virtual_list_open_)
                    throw std::logic_error("ImGui UI backend has no virtual list to end");
                if (list_clipper_started_)
                {
                    list_clipper_.End();
                    list_clipper_started_ = false;
                }
                ImGui::EndChild();
                PopUiId();
                virtual_list_open_ = false;
            }

            void SetClipboardText(const std::string_view text) override
            {
                const std::string null_terminated{text};
                ImGui::SetClipboardText(null_terminated.c_str());
            }

            UiColor TextColor(const UiTextRole role) const noexcept
            {
                if (theme_stack_.empty())
                {
                    const ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                    return {color.x, color.y, color.z, color.w};
                }
                const UiTheme &theme = theme_stack_.back();
                switch (role)
                {
                case UiTextRole::Muted: return theme.muted;
                case UiTextRole::Accent: return theme.accent;
                case UiTextRole::Warning: return theme.accent_active;
                case UiTextRole::Error: return theme.error;
                case UiTextRole::Success: return theme.success;
                case UiTextRole::Primary: return theme.text;
                }
                return theme.text;
            }

            void StyledText(const std::string_view text, const UiTextRole role) override
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Color(TextColor(role)));
                Text(text);
                ImGui::PopStyleColor();
            }

            static void DrawIcon(const UiIconMask icon, const ImVec2 origin, const float size)
            {
                const float dx = size / static_cast<float>(icon.width);
                const float dy = size / static_cast<float>(icon.height);
                const ImU32 tint = ImGui::GetColorU32(ImGuiCol_Text);
                ImDrawList *const draw = ImGui::GetWindowDrawList();
                for (std::uint32_t y = 0; y < icon.height; ++y)
                {
                    for (std::uint32_t x = 0; x < icon.width; ++x)
                    {
                        const auto alpha = icon.alpha[static_cast<std::size_t>(y) * icon.width + x];
                        if (alpha == 0) continue;
                        const ImU32 opacity = ((tint >> IM_COL32_A_SHIFT) * alpha) / 255U;
                        const ImVec2 point{origin.x + x * dx, origin.y + y * dy};
                        draw->AddRectFilled(point, {point.x + dx, point.y + dy},
                            (tint & ~IM_COL32_A_MASK) | (opacity << IM_COL32_A_SHIFT));
                    }
                }
            }

            UiFrameResult EndFrame(const std::uint64_t frame) override
            {
                return {frame, true, ImGui::GetIO().WantTextInput,
                        ImGui::GetIO().WantCaptureMouse,
                        observe_ ? observations_ : std::vector<UiNodeObservation>{}};
            }

            void AbortFrame() noexcept override
            {
                while (!theme_stack_.empty())
                    PopTheme();
                observations_.clear();
            }

            void Shutdown() noexcept override
            {
                text_buffers_.clear();
                text_active_.clear();
                while (!theme_stack_.empty())
                    PopTheme();
            }

            void Observe(const UiControl &control, const UiNodeId parent,
                         const std::optional<UiActionKind> action = std::nullopt)
            {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                observations_.push_back({session_, frame_, control.id, parent, control.role,
                    control.label, control.description, control.value, control.enabled,
                    control.disabled_reason, ImGui::IsItemFocused(), ImGui::IsItemHovered(),
                    ImGui::IsItemVisible() == false,
                    {{min.x, min.y}, {max.x, max.y}}, UiGeometryProvenance::ActualBackend,
                    ImGui::IsItemActive()});
                observations_.back().actions = control.actions;
                observations_.back().last_action = action;
            }

            ImFont *code_font_ = nullptr; // Borrowed from the host font atlas.
            UiSessionId session_ = 0;
            std::uint64_t frame_ = 0;
            UiNodeId window_id_ = 0;
            UiNodeId active_canvas_id_ = 0;
            bool observe_ = false;
            bool window_padding_open_ = false;
            bool window_border_open_ = false;
            bool virtual_list_open_ = false;
            bool list_clipper_started_ = false;
            ImGuiListClipper list_clipper_;
            std::vector<UiTheme> theme_stack_;
            UiViewportInfo viewport_{};
            std::unordered_map<UiNodeId, UiViewportInfo> window_viewports_;
            std::unordered_map<UiNodeId, UiRect> window_bounds_;
            std::unordered_map<UiNodeId, std::string> text_buffers_;
            std::unordered_map<UiNodeId, bool> text_active_;
            std::vector<UiNodeObservation> observations_;
            UiImageDrawCallback draw_image_;
        };
    }

    std::unique_ptr<IUiBackend> CreateImGuiInlineBackend()
    {
        return CreateImGuiInlineBackend(nullptr);
    }

    std::unique_ptr<IUiBackend> CreateImGuiInlineBackend(
        ImFont *code_font, UiImageDrawCallback draw_image)
    {
        return std::make_unique<ImGuiInlineBackend>(code_font, std::move(draw_image));
    }
}
