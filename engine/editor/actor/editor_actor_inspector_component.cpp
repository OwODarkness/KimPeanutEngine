#include "editor/actor/editor_actor_inspector_component.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor/actor/actor_property_widget.h"
#include "runtime/input/input_system.h"
#include "runtime/runtime_camera_control.h"
#include "runtime/window/window_system.h"

namespace kpengine::editor
{
    namespace
    {
        std::string ShortTypeName(std::string_view name)
        {
            const std::size_t separator = name.find_last_of('.');
            return std::string(separator == std::string_view::npos ? name
                                                                     : name.substr(separator + 1));
        }

        std::string PropertyLabel(const reflection::ReflectionPropertyDescriptor &descriptor)
        {
            return descriptor.metadata.display_name.empty() ? descriptor.name
                                                             : descriptor.metadata.display_name;
        }

        struct TransformGroupSpec
        {
            const char *label;
            std::array<const char *, 3> property_names;
            std::array<const char *, 3> tooltips;
        };

        const TransformGroupSpec *FindTransformGroupSpec(std::string_view property_name)
        {
            static const std::array<TransformGroupSpec, 3> specs = {{
                {"Location",
                 {"transform.location.x", "transform.location.y", "transform.location.z"},
                 {"X", "Y", "Z"}},
                {"Rotation",
                 {"transform.rotation.pitch", "transform.rotation.yaw", "transform.rotation.roll"},
                 {"Pitch", "Yaw", "Roll"}},
                {"Scale",
                 {"transform.scale.x", "transform.scale.y", "transform.scale.z"},
                 {"X", "Y", "Z"}},
            }};

            for (const TransformGroupSpec &spec : specs)
            {
                if (property_name == spec.property_names.front())
                {
                    return &spec;
                }
            }
            return nullptr;
        }

        const std::array<const char *, 3> &LightColorPropertyNames()
        {
            static const std::array<const char *, 3> names = {{
                "light.color.r", "light.color.g", "light.color.b"}};
            return names;
        }

        bool IsLightColorProperty(std::string_view property_name)
        {
            const auto &names = LightColorPropertyNames();
            return std::find(names.begin(), names.end(), property_name) != names.end();
        }

        struct TransformChannel
        {
            const reflection::ReflectionPropertyDescriptor *descriptor = nullptr;
            const gameplay::PropertyValueSnapshot *snapshot = nullptr;
        };

        TransformChannel FindTransformChannel(
            const gameplay::ComponentEditorSnapshot &component,
            const reflection::IReflectionCatalog &catalog,
            std::string_view property_name)
        {
            for (const gameplay::PropertyValueSnapshot &property : component.properties)
            {
                const reflection::ReflectionPropertyDescriptor *const descriptor =
                    catalog.FindProperty(component.type, property.property);
                if (descriptor != nullptr && descriptor->name == property_name)
                {
                    return {descriptor, &property};
                }
            }
            return {};
        }

        bool ContainsPropertyId(const std::vector<reflection::ReflectionPropertyId> &ids,
                                reflection::ReflectionPropertyId id)
        {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }

        ImVec4 TransformAxisColor(std::size_t channel_index)
        {
            static const std::array<ImVec4, 3> colors = {{
                ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                ImVec4(0.35f, 0.88f, 0.50f, 1.0f),
                ImVec4(0.35f, 0.62f, 1.0f, 1.0f),
            }};
            return colors[channel_index];
        }

        void RenderReadOnlyValue(const gameplay::PropertyValueSnapshot &snapshot,
                                 std::string_view diagnostic = {})
        {
            ImGui::TextDisabled("%s", FormatReflectionValue(snapshot.value).c_str());
            const std::string_view message = diagnostic.empty() ? snapshot.diagnostic : diagnostic;
            if (!message.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(%.*s)", static_cast<int>(message.size()), message.data());
            }
        }

        bool ReadSigned(const reflection::ReflectionValue &value, int64_t &result)
        {
            if (const int64_t *const signed_value = value.TryGet<int64_t>())
            {
                result = *signed_value;
                return true;
            }
            return false;
        }

        bool ReadUnsigned(const reflection::ReflectionValue &value, uint64_t &result)
        {
            if (const uint64_t *const unsigned_value = value.TryGet<uint64_t>())
            {
                result = *unsigned_value;
                return true;
            }
            return false;
        }

        bool EnumOptionMatches(const reflection::ReflectionValue &value, int64_t option)
        {
            if (const int64_t *const signed_value = value.TryGet<int64_t>())
            {
                return *signed_value == option;
            }
            if (const uint64_t *const unsigned_value = value.TryGet<uint64_t>())
            {
                return option >= 0 && *unsigned_value == static_cast<uint64_t>(option);
            }
            return false;
        }

        void SubmitValue(ActorEditorModel &model,
                         const gameplay::ActorEditorSnapshot &actor,
                         const gameplay::ComponentEditorSnapshot &component,
                         const reflection::ReflectionPropertyDescriptor &descriptor,
                         reflection::ReflectionValue value)
        {
            model.SubmitPropertyEdit(actor.actor, component.component, component.type,
                                     descriptor.id, std::move(value));
        }

        double ReadFloatingValue(const gameplay::PropertyValueSnapshot &snapshot)
        {
            if (const double *const floating = snapshot.value.TryGet<double>())
            {
                return *floating;
            }
            return 0.0;
        }

        double &FindNumericDraft(
            std::unordered_map<ActorEditorPropertyKey, double, ActorEditorPropertyKeyHash> &drafts,
            const ActorEditorPropertyKey &key,
            const gameplay::PropertyValueSnapshot &snapshot)
        {
            const auto [iterator, inserted] = drafts.emplace(key, ReadFloatingValue(snapshot));
            (void)inserted;
            return iterator->second;
        }

        void RenderEnum(ActorEditorModel &model,
                        const gameplay::ActorEditorSnapshot &actor,
                        const gameplay::ComponentEditorSnapshot &component,
                        const reflection::ReflectionPropertyDescriptor &descriptor,
                        const gameplay::PropertyValueSnapshot &snapshot,
                        bool enabled)
        {
            std::string current_label = "Unknown (" + FormatReflectionValue(snapshot.value) + ")";
            for (const reflection::ReflectionEnumOption &option : descriptor.metadata.enum_options)
            {
                if (EnumOptionMatches(snapshot.value, option.value))
                {
                    current_label = option.label;
                    break;
                }
            }
            if (!enabled)
            {
                ImGui::TextDisabled("%s", current_label.c_str());
                return;
            }
            if (ImGui::BeginCombo("##enum", current_label.c_str()))
            {
                for (const reflection::ReflectionEnumOption &option : descriptor.metadata.enum_options)
                {
                    const bool selected = EnumOptionMatches(snapshot.value, option.value);
                    if (ImGui::Selectable(option.label.c_str(), selected))
                    {
                        const reflection::ReflectionValue value =
                            descriptor.value_type == reflection::ReflectionValueType::UnsignedInteger &&
                                    option.value >= 0
                                ? reflection::ReflectionValue{static_cast<uint64_t>(option.value)}
                                : reflection::ReflectionValue{option.value};
                        SubmitValue(model, actor, component, descriptor, value);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        bool RenderTransformChannel(
            ActorEditorModel &model,
            const gameplay::ActorEditorSnapshot &actor,
            const gameplay::ComponentEditorSnapshot &component,
            const TransformGroupSpec &group,
            std::size_t channel_index,
            const TransformChannel &channel,
            std::unordered_map<ActorEditorPropertyKey, double, ActorEditorPropertyKeyHash>
                &numeric_drafts,
            float input_width)
        {
            const reflection::ReflectionPropertyDescriptor &descriptor = *channel.descriptor;
            const gameplay::PropertyValueSnapshot &snapshot = *channel.snapshot;
            const ActorEditorPropertyKey key{actor.actor, component.component, descriptor.id};
            const ActorPropertyWidgetPolicy policy = ResolveActorPropertyWidget(descriptor, snapshot);
            const bool pending = model.IsPropertyPending(key);
            const bool has_draft = numeric_drafts.find(key) != numeric_drafts.end();

            ImGui::PushID(static_cast<int>(descriptor.id.value));
            if (!policy.editable || (pending && !has_draft) ||
                policy.kind != ActorPropertyWidgetKind::FloatingPoint)
            {
                numeric_drafts.erase(key);
                RenderReadOnlyValue(snapshot, policy.diagnostic);
            }
            else
            {
                double &value = FindNumericDraft(numeric_drafts, key, snapshot);
                const double *minimum = descriptor.metadata.minimum.has_value()
                                            ? &*descriptor.metadata.minimum
                                            : nullptr;
                const double *maximum = descriptor.metadata.maximum.has_value()
                                            ? &*descriptor.metadata.maximum
                                            : nullptr;
                const float step = descriptor.metadata.step.has_value() &&
                                           *descriptor.metadata.step > 0.0
                                       ? static_cast<float>(*descriptor.metadata.step)
                                       : 0.1f;
                ImGui::SetNextItemWidth(input_width);
                ImGui::DragScalar("##transform", ImGuiDataType_Double, &value, step, minimum,
                                  maximum, "%.3f");
                const bool value_active = ImGui::IsItemActive();
                const bool edited = ImGui::IsItemDeactivatedAfterEdit();
                const bool pending_after_input = model.IsPropertyPending(key);
                if (!pending_after_input && value != ReadFloatingValue(snapshot) &&
                    (value_active || edited))
                {
                    SubmitValue(model, actor, component, descriptor,
                                reflection::ReflectionValue{value});
                }
                if (edited || ImGui::IsItemDeactivated())
                {
                    numeric_drafts.erase(key);
                }
            }

            const ImVec2 value_min = ImGui::GetItemRectMin();
            const ImVec2 value_max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(value_min.x, value_max.y - 2.0f),
                ImVec2(value_max.x, value_max.y),
                ImGui::GetColorU32(TransformAxisColor(channel_index)));
            const bool value_hovered = ImGui::IsItemHovered();
            const bool value_active = ImGui::IsItemActive();
            if (value_hovered || value_active)
            {
                ImGui::SetNextFrameWantCaptureMouse(true);
            }
            if (value_hovered && !value_active)
            {
                ImGui::SetTooltip("%s", group.tooltips[channel_index]);
            }
            ImGui::PopID();
            return value_active;
        }

        bool RenderTransformGroup(
            ActorEditorModel &model,
            const gameplay::ActorEditorSnapshot &actor,
            const gameplay::ComponentEditorSnapshot &component,
            const TransformGroupSpec &group,
            const std::array<TransformChannel, 3> &channels,
            std::unordered_map<ActorEditorPropertyKey, double, ActorEditorPropertyKeyHash>
                &numeric_drafts)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", group.label);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Edit local %s", group.label);
            }

            const float available_width = ImGui::GetContentRegionAvail().x;
            const float input_width = std::max(36.0f, (available_width - 82.0f) / 3.0f);
            ImGui::SameLine(82.0f);
            bool active = false;
            for (std::size_t channel_index = 0; channel_index < channels.size(); ++channel_index)
            {
                if (channel_index != 0)
                {
                    ImGui::SameLine(0.0f, 5.0f);
                }
                active = RenderTransformChannel(model, actor, component, group, channel_index,
                                                channels[channel_index], numeric_drafts,
                                                input_width) ||
                         active;
            }
            return active;
        }

        void RenderLightColorGroup(
            ActorEditorModel &model,
            const gameplay::ActorEditorSnapshot &actor,
            const gameplay::ComponentEditorSnapshot &component,
            const std::array<TransformChannel, 3> &channels,
            std::unordered_map<ActorEditorPropertyKey, double, ActorEditorPropertyKeyHash>
                &numeric_drafts)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Color");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Light color");
            }
            ImGui::SameLine(82.0f);

            float color[3] = {};
            for (std::size_t channel_index = 0; channel_index < channels.size(); ++channel_index)
            {
                const ActorEditorPropertyKey key{
                    actor.actor, component.component, channels[channel_index].descriptor->id};
                const auto draft = numeric_drafts.find(key);
                const double value = draft != numeric_drafts.end()
                                         ? draft->second
                                         : ReadFloatingValue(*channels[channel_index].snapshot);
                color[channel_index] = static_cast<float>(std::clamp(value, 0.0, 1.0));
            }

            ImGui::PushID(static_cast<int>(component.component.value));
            if (ImGui::ColorButton("##light_color", ImVec4(color[0], color[1], color[2], 1.0f),
                                   ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(96.0f, ImGui::GetFrameHeight())))
            {
                ImGui::OpenPopup("##light_color_picker");
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Light color");
            }

            const bool picker_open = ImGui::BeginPopup("##light_color_picker");
            if (picker_open)
            {
                const bool changed = ImGui::ColorPicker3(
                    "##picker", color, ImGuiColorEditFlags_DisplayRGB |
                                             ImGuiColorEditFlags_DisplayHSV);
                for (std::size_t channel_index = 0; channel_index < channels.size();
                     ++channel_index)
                {
                    const ActorEditorPropertyKey key{
                        actor.actor, component.component, channels[channel_index].descriptor->id};
                    const double authoritative =
                        ReadFloatingValue(*channels[channel_index].snapshot);
                    const auto draft = numeric_drafts.find(key);
                    if (!changed && draft == numeric_drafts.end())
                    {
                        continue;
                    }

                    const double value = static_cast<double>(color[channel_index]);
                    numeric_drafts[key] = value;
                    if (!model.IsPropertyPending(key) && value != authoritative)
                    {
                        SubmitValue(model, actor, component, *channels[channel_index].descriptor,
                                    reflection::ReflectionValue{value});
                    }
                }
                ImGui::EndPopup();
            }

            if (!picker_open)
            {
                for (const TransformChannel &channel : channels)
                {
                    const ActorEditorPropertyKey key{
                        actor.actor, component.component, channel.descriptor->id};
                    const auto draft = numeric_drafts.find(key);
                    const ActorEditorPropertyFeedback *const feedback =
                        model.FindPropertyFeedback(key);
                    const bool rejected = feedback != nullptr &&
                                          feedback->kind == ActorEditorFeedbackKind::Rejected;
                    if (draft != numeric_drafts.end() &&
                        (rejected || (!model.IsPropertyPending(key) &&
                                      draft->second == ReadFloatingValue(*channel.snapshot))))
                    {
                        numeric_drafts.erase(draft);
                    }
                }
            }
            ImGui::PopID();
        }

        void RenderProperty(ActorEditorModel &model,
                            const gameplay::ActorEditorSnapshot &actor,
                            const gameplay::ComponentEditorSnapshot &component,
                            const reflection::ReflectionPropertyDescriptor &descriptor,
                            const gameplay::PropertyValueSnapshot &snapshot,
                            std::unordered_map<ActorEditorPropertyKey, double,
                                               ActorEditorPropertyKeyHash> &numeric_drafts)
        {
            const ActorEditorPropertyKey key{actor.actor, component.component, descriptor.id};
            const ActorPropertyWidgetPolicy policy = ResolveActorPropertyWidget(descriptor, snapshot);
            const bool pending = model.IsPropertyPending(key);
            const bool has_numeric_draft = numeric_drafts.find(key) != numeric_drafts.end();
            ImGui::PushID(static_cast<int>(descriptor.id.value));
            ImGui::Text("%s", PropertyLabel(descriptor).c_str());
            if (!descriptor.metadata.tooltip.empty() && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", descriptor.metadata.tooltip.c_str());
            }
            ImGui::SameLine(180.0f);
            if (!policy.editable || (pending && !has_numeric_draft))
            {
                numeric_drafts.erase(key);
                RenderReadOnlyValue(snapshot, policy.diagnostic);
            }
            else
            {
                switch (policy.kind)
                {
                case ActorPropertyWidgetKind::Checkbox:
                {
                    bool value = snapshot.value.TryGet<bool>() != nullptr &&
                                 *snapshot.value.TryGet<bool>();
                    if (ImGui::Checkbox("##bool", &value))
                    {
                        SubmitValue(model, actor, component, descriptor,
                                    reflection::ReflectionValue{value});
                    }
                    break;
                }
                case ActorPropertyWidgetKind::EnumCombo:
                    RenderEnum(model, actor, component, descriptor, snapshot, true);
                    break;
                case ActorPropertyWidgetKind::SignedInteger:
                {
                    int64_t value = 0;
                    if (!ReadSigned(snapshot.value, value))
                    {
                        RenderReadOnlyValue(snapshot, "Invalid signed integer value");
                        break;
                    }
                    if (ImGui::InputScalar("##signed", ImGuiDataType_S64, &value, nullptr, nullptr,
                                           "%" PRId64) &&
                        ImGui::IsItemDeactivatedAfterEdit())
                    {
                        SubmitValue(model, actor, component, descriptor,
                                    reflection::ReflectionValue{value});
                    }
                    break;
                }
                case ActorPropertyWidgetKind::UnsignedInteger:
                {
                    uint64_t value = 0;
                    if (!ReadUnsigned(snapshot.value, value))
                    {
                        RenderReadOnlyValue(snapshot, "Invalid unsigned integer value");
                        break;
                    }
                    if (ImGui::InputScalar("##unsigned", ImGuiDataType_U64, &value, nullptr, nullptr,
                                           "%" PRIu64) &&
                        ImGui::IsItemDeactivatedAfterEdit())
                    {
                        SubmitValue(model, actor, component, descriptor,
                                    reflection::ReflectionValue{value});
                    }
                    break;
                }
                case ActorPropertyWidgetKind::FloatingPoint:
                {
                    double &value = FindNumericDraft(numeric_drafts, key, snapshot);
                    const double *minimum = descriptor.metadata.minimum.has_value()
                                                ? &*descriptor.metadata.minimum
                                                : nullptr;
                    const double *maximum = descriptor.metadata.maximum.has_value()
                                                ? &*descriptor.metadata.maximum
                                                : nullptr;
                    const float step = descriptor.metadata.step.has_value() &&
                                               *descriptor.metadata.step > 0.0
                                           ? static_cast<float>(*descriptor.metadata.step)
                                           : 0.01f;
                    ImGui::DragScalar("##float", ImGuiDataType_Double, &value, step, minimum,
                                      maximum, "%.6f");
                    const bool value_active = ImGui::IsItemActive();
                    const bool edited = ImGui::IsItemDeactivatedAfterEdit();
                    const bool pending_after_input = model.IsPropertyPending(key);
                    if (!pending_after_input && value != ReadFloatingValue(snapshot) &&
                        (value_active || edited))
                    {
                        SubmitValue(model, actor, component, descriptor,
                                    reflection::ReflectionValue{value});
                    }
                    if (edited || ImGui::IsItemDeactivated())
                    {
                        numeric_drafts.erase(key);
                    }
                    break;
                }
                case ActorPropertyWidgetKind::String:
                {
                    const std::string *const current = snapshot.value.TryGet<std::string>();
                    std::string *const draft = model.FindOrCreateStringDraft(
                        key, current != nullptr ? std::string_view(*current) : std::string_view{});
                    if (draft == nullptr)
                    {
                        RenderReadOnlyValue(snapshot, "String draft budget exhausted");
                        break;
                    }
                    draft->reserve(513);
                    draft->resize(std::min<std::size_t>(draft->size(), 512U) + 1U, '\0');
                    const bool submitted = ImGui::InputText(
                        "##string", draft->data(), draft->size(),
                        ImGuiInputTextFlags_EnterReturnsTrue);
                    const std::size_t length = std::char_traits<char>::length(draft->c_str());
                    draft->resize(length);
                    if (submitted || ImGui::IsItemDeactivatedAfterEdit())
                    {
                        SubmitValue(model, actor, component, descriptor,
                                    reflection::ReflectionValue{*draft});
                    }
                    break;
                }
                case ActorPropertyWidgetKind::ReadOnly:
                    RenderReadOnlyValue(snapshot, policy.diagnostic);
                    break;
                }
            }
            ImGui::PopID();
        }
    }

    EditorActorInspectorComponent::EditorActorInspectorComponent(
        ActorEditorModel &model, WindowSystem *window_system, input::InputSystem *input_system,
        runtime::ISceneCameraControlSink *camera_control_sink)
        : EditorWindowComponent("Actor Inspector", EditorWindowConfig{0.0f, 0.28f, 0.22f, 0.42f,
                                                                        true}),
          model_(model), window_system_(window_system), input_system_(input_system),
          camera_control_sink_(camera_control_sink)
    {
    }

    EditorActorInspectorComponent::~EditorActorInspectorComponent()
    {
        ReleaseValueMouseCapture();
    }

    void EditorActorInspectorComponent::ReleaseValueMouseCapture()
    {
        if (!value_mouse_captured_)
        {
            return;
        }
        if (window_system_ != nullptr)
        {
            window_system_->SetMouseCapture(false);
        }
        if (input_system_ != nullptr)
        {
            input_system_->ResetCursorTracking();
        }
        value_mouse_captured_ = false;
    }

    void EditorActorInspectorComponent::UpdateValueMouseCapture(bool transform_drag_active)
    {
        const ImGuiIO &io = ImGui::GetIO();
        const bool should_capture = transform_drag_active &&
                                     ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                                     ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
                                     !io.WantTextInput;
        if (should_capture && !value_mouse_captured_ && window_system_ != nullptr)
        {
            // Editing a reflected value takes ownership away from the scene camera
            // before switching GLFW to relative mouse motion mode.
            if (camera_control_sink_ != nullptr)
            {
                camera_control_sink_->SetSceneCameraControlCaptured(false);
            }
            window_system_->SetMouseCapture(true);
            if (input_system_ != nullptr)
            {
                input_system_->ResetCursorTracking();
            }
            value_mouse_captured_ = true;
        }
        else if (!should_capture)
        {
            ReleaseValueMouseCapture();
        }
    }

    void EditorActorInspectorComponent::RenderContent()
    {
        const auto &snapshot = model_.GetSnapshot();
        const std::optional<gameplay::ActorHandle> selection = model_.GetSelection();
        if (!snapshot || !selection.has_value())
        {
            UpdateValueMouseCapture(false);
            ImGui::TextDisabled("Select an actor in World Outliner");
            return;
        }
        const gameplay::ActorEditorSnapshot *const actor = model_.FindActor(*selection);
        if (actor == nullptr)
        {
            UpdateValueMouseCapture(false);
            ImGui::TextDisabled("Select an actor in World Outliner");
            return;
        }
        ImGui::PushID(static_cast<int>(actor->actor.id));
        ImGui::PushID(static_cast<int>(actor->actor.generation));
        ImGui::Text("%s", actor->display_name.empty() ? "Actor" : actor->display_name.c_str());
        ImGui::TextDisabled("Handle %u:%u  State %u", actor->actor.id,
                            static_cast<unsigned int>(actor->actor.generation),
                            static_cast<unsigned int>(actor->state));
        if (snapshot->truncated || snapshot->omitted.components != 0 ||
            snapshot->omitted.properties != 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                               "Snapshot omissions: components %zu, properties %zu",
                               snapshot->omitted.components, snapshot->omitted.properties);
        }

        const reflection::IReflectionCatalog *const catalog = model_.GetCatalog();
        bool transform_drag_active = false;
        for (const gameplay::ComponentEditorSnapshot &component : actor->components)
        {
            ImGui::PushID(static_cast<int>(component.component.value));
            const reflection::ReflectionTypeDescriptor *const type =
                catalog != nullptr && component.type.IsValid() ? catalog->FindType(component.type)
                                                                : nullptr;
            const std::string title = type != nullptr ? ShortTypeName(type->name) : "Unreflected Component";
            std::string section = title + "  [" + std::to_string(component.component.value) + "]";
            if (component.is_root)
            {
                section += "  Root";
            }
            if (ImGui::CollapsingHeader(section.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (type == nullptr)
                {
                    ImGui::TextDisabled("%s", component.diagnostic.empty()
                                                 ? "Component type is unavailable"
                                                 : component.diagnostic.c_str());
                }
                else
                {
                    std::string previous_category;
                    std::vector<reflection::ReflectionPropertyId> grouped_transform_properties;
                    for (const gameplay::PropertyValueSnapshot &property : component.properties)
                    {
                        const reflection::ReflectionPropertyDescriptor *const descriptor =
                            catalog->FindProperty(component.type, property.property);
                        if (descriptor == nullptr)
                        {
                            ImGui::TextDisabled("Unknown property %u: %s", property.property.value,
                                                property.diagnostic.c_str());
                            continue;
                        }
                        if (ContainsPropertyId(grouped_transform_properties, descriptor->id))
                        {
                            continue;
                        }
                        const std::string category = descriptor->metadata.category;
                        if (!category.empty() && category != previous_category)
                        {
                            ImGui::SeparatorText(category.c_str());
                            previous_category = category;
                        }

                        const std::array<const char *, 3> &color_property_names =
                            LightColorPropertyNames();
                        if (IsLightColorProperty(descriptor->name))
                        {
                            std::array<TransformChannel, 3> color_channels{};
                            bool complete = true;
                            for (std::size_t channel_index = 0;
                                 channel_index < color_channels.size(); ++channel_index)
                            {
                                color_channels[channel_index] = FindTransformChannel(
                                    component, *catalog, color_property_names[channel_index]);
                                complete = complete &&
                                           color_channels[channel_index].descriptor != nullptr &&
                                           color_channels[channel_index].snapshot != nullptr;
                            }
                            if (complete)
                            {
                                RenderLightColorGroup(model_, *actor, component, color_channels,
                                                      numeric_drafts_);
                                for (const TransformChannel &channel : color_channels)
                                {
                                    grouped_transform_properties.push_back(channel.descriptor->id);
                                }
                                continue;
                            }
                        }

                        const TransformGroupSpec *const transform_group =
                            FindTransformGroupSpec(descriptor->name);
                        if (transform_group != nullptr)
                        {
                            std::array<TransformChannel, 3> channels{};
                            bool complete = true;
                            for (std::size_t channel_index = 0;
                                 channel_index < channels.size(); ++channel_index)
                            {
                                channels[channel_index] = FindTransformChannel(
                                    component, *catalog,
                                    transform_group->property_names[channel_index]);
                                complete = complete && channels[channel_index].descriptor != nullptr &&
                                           channels[channel_index].snapshot != nullptr;
                            }
                            if (complete)
                            {
                                transform_drag_active =
                                    RenderTransformGroup(model_, *actor, component, *transform_group,
                                                         channels, numeric_drafts_) ||
                                    transform_drag_active;
                                for (const TransformChannel &channel : channels)
                                {
                                    grouped_transform_properties.push_back(channel.descriptor->id);
                                }
                                continue;
                            }
                        }
                        RenderProperty(model_, *actor, component, *descriptor, property,
                                       numeric_drafts_);
                    }
                }
            }
            ImGui::PopID();
        }
        UpdateValueMouseCapture(transform_drag_active);
        ImGui::PopID();
        ImGui::PopID();
    }
}
