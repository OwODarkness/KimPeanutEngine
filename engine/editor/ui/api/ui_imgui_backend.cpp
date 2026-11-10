#include "editor_ui/ui_imgui_backend.h"

#include <imgui.h>

#include <stdexcept>
#include <bit>
#include <cstdint>
#include <type_traits>
#include <algorithm>
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

        struct ImGuiInlineBackend final : IUiBackend
        {
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
                observations_.clear();
                window_id_ = 0;
            }

            bool BeginWindow(const UiControl &window) override
            {
                window_id_ = window.id;
                const bool visible = ImGui::Begin(window.label.c_str());
                if (!observe_) return visible;
                const ImVec2 position = ImGui::GetWindowPos();
                const ImVec2 size = ImGui::GetWindowSize();
                observations_.push_back({session_, frame_, window.id, 0, window.role,
                    window.label, window.description, window.value, window.enabled,
                    window.disabled_reason, ImGui::IsWindowFocused(),
                    ImGui::IsWindowHovered(), false,
                    {{position.x, position.y}, {position.x + size.x, position.y + size.y}},
                    UiGeometryProvenance::ActualBackend, false});
                return visible;
            }

            void EndWindow() override { ImGui::End(); window_id_ = 0; }

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
                UiEditResult result{control.value, false, false, false};
                const auto id_bits = static_cast<std::uint64_t>(control.id);
                ImGui::PushID(std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(id_bits >> 32U)));
                ImGui::PushID(std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(id_bits)));
                if (!control.enabled) ImGui::BeginDisabled();
                switch (control.role)
                {
                case UiRole::Button:
                    result.activated = ImGui::Button(control.label.c_str());
                    if (result.activated) result.action = UiActionKind::Activate;
                    break;
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
                    result.activated = ImGui::Selectable(control.label.c_str(), selected);
                    result.value = result.activated ? UiValue{true} : control.value;
                    result.changed = result.activated && !selected;
                    result.committed = result.activated;
                    if (result.activated) result.action = UiActionKind::Select;
                    break;
                }
                case UiRole::NumericInput:
                {
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
                                return ImGui::DragScalar(control.label.c_str(), DataType(control.value),
                                                         &typed, WidgetSpeed(control.step),
                                                         minimum, maximum);
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
                    const std::string &source = std::get<std::string>(control.value);
                    auto [buffer, inserted] = text_buffers_.try_emplace(control.id, source);
                    const bool was_active = text_active_[control.id];
                    if (!inserted && !was_active && buffer->second != source)
                        buffer->second = source;
                    buffer->second.resize(4096, '\0');
                    (void)ImGui::InputText(control.label.c_str(), buffer->second.data(),
                                           buffer->second.size());
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
                    const char *preview = selected == control.choices.end()
                                              ? "Unavailable"
                                              : selected->label.c_str();
                    if (ImGui::BeginCombo(control.label.c_str(), preview))
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

            void Text(const std::string_view text) override
            {
                ImGui::TextUnformatted(text.data(), text.data() + text.size());
            }

            UiFrameResult EndFrame(const std::uint64_t frame) override
            {
                return {frame, true, ImGui::GetIO().WantTextInput,
                        ImGui::GetIO().WantCaptureMouse,
                        observe_ ? observations_ : std::vector<UiNodeObservation>{}};
            }

            void AbortFrame() noexcept override { observations_.clear(); }

            void Shutdown() noexcept override
            {
                text_buffers_.clear();
                text_active_.clear();
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

            UiSessionId session_ = 0;
            std::uint64_t frame_ = 0;
            UiNodeId window_id_ = 0;
            bool observe_ = false;
            std::unordered_map<UiNodeId, std::string> text_buffers_;
            std::unordered_map<UiNodeId, bool> text_active_;
            std::vector<UiNodeObservation> observations_;
        };
    }

    std::unique_ptr<IUiBackend> CreateImGuiInlineBackend()
    {
        return std::make_unique<ImGuiInlineBackend>();
    }
}
