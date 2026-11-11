#include "settings_property_fields_view.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace kpengine::editor
{
    namespace
    {
        ui::UiValue ToUiValue(const reflection::ReflectionValue &value)
        {
            switch (value.GetType())
            {
            case reflection::ReflectionValueType::Bool: return *value.TryGet<bool>();
            case reflection::ReflectionValueType::SignedInteger: return *value.TryGet<std::int64_t>();
            case reflection::ReflectionValueType::UnsignedInteger: return *value.TryGet<std::uint64_t>();
            case reflection::ReflectionValueType::FloatingPoint: return *value.TryGet<double>();
            case reflection::ReflectionValueType::String: return *value.TryGet<std::string>();
            }
            return false;
        }

        template <typename T>
        ui::UiEditResult ToEditResult(const ui::UiTypedEditResult<T> &result)
        {
            return {ui::UiValue{result.value}, result.changed, result.committed, result.cancelled,
                    result.activated, result.action};
        }

        std::optional<std::int64_t> SignedBound(const std::optional<double> bound)
        {
            if (!bound || !std::isfinite(*bound) || std::trunc(*bound) != *bound)
                return std::nullopt;
            const long double wide = *bound;
            if (wide < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                wide >= 9223372036854775808.0L)
                return std::nullopt;
            return static_cast<std::int64_t>(*bound);
        }

        std::optional<std::uint64_t> UnsignedBound(const std::optional<double> bound)
        {
            if (!bound || !std::isfinite(*bound) || std::trunc(*bound) != *bound || *bound < 0.0)
                return std::nullopt;
            const long double wide = *bound;
            if (wide >= 18446744073709551616.0L)
                return std::nullopt;
            return static_cast<std::uint64_t>(*bound);
        }
    }

    ui::UiNodeId SettingsPropertyFieldsView::StableId(
        const std::string_view section_key, const std::string_view property_key) noexcept
    {
        constexpr std::uint64_t offset = 14695981039346656037ULL;
        constexpr std::uint64_t prime = 1099511628211ULL;
        std::uint64_t hash = offset;
        for (const char ch : section_key)
            hash = (hash ^ static_cast<unsigned char>(ch)) * prime;
        hash = (hash ^ static_cast<unsigned char>('.')) * prime;
        for (const char ch : property_key)
            hash = (hash ^ static_cast<unsigned char>(ch)) * prime;
        return hash == 0 ? 1 : hash;
    }

    ui::UiEditResult SettingsPropertyFieldsView::Draw(
        ui::UiContext &context, const std::string_view section_key,
        const settings::SettingPropertyDefinition &property,
        const reflection::ReflectionValue &value, const bool writable)
    {
        using namespace reflection;
        ui::UiControl control{};
        control.id = StableId(section_key, property.key);
        control.label = property.metadata.display_name.empty()
                            ? property.key
                            : property.metadata.display_name;
        control.description = property.metadata.tooltip;
        control.value = ToUiValue(value);
        control.enabled = writable;
        if (!control.enabled)
            control.disabled_reason = "Read-only setting";

        if (property.metadata.semantic == ReflectionWidgetSemantic::Enum)
        {
            control.role = ui::UiRole::Combo;
            control.actions = {ui::UiActionKind::Select};
            for (const ReflectionEnumOption &option : property.metadata.enum_options)
            {
                control.choices.push_back({std::to_string(option.value), option.label,
                                           ui::UiValue{option.value}});
            }
        }
        else
        {
            switch (property.value_type)
            {
            case ReflectionValueType::Bool: control.role = ui::UiRole::CheckBox; break;
            case ReflectionValueType::SignedInteger:
                control.role = ui::UiRole::NumericInput;
                if (const auto bound = SignedBound(property.metadata.minimum)) control.minimum = *bound;
                if (const auto bound = SignedBound(property.metadata.maximum)) control.maximum = *bound;
                if (property.metadata.step && *property.metadata.step >= 0.0)
                    if (const auto step = SignedBound(property.metadata.step)) control.step = *step;
                break;
            case ReflectionValueType::UnsignedInteger:
                control.role = ui::UiRole::NumericInput;
                if (const auto bound = UnsignedBound(property.metadata.minimum)) control.minimum = *bound;
                if (const auto bound = UnsignedBound(property.metadata.maximum)) control.maximum = *bound;
                if (property.metadata.step && *property.metadata.step >= 0.0)
                    if (const auto step = UnsignedBound(property.metadata.step)) control.step = *step;
                break;
            case ReflectionValueType::FloatingPoint:
                control.role = ui::UiRole::NumericInput;
                if (property.metadata.minimum && std::isfinite(*property.metadata.minimum))
                    control.minimum = *property.metadata.minimum;
                if (property.metadata.maximum && std::isfinite(*property.metadata.maximum))
                    control.maximum = *property.metadata.maximum;
                if (property.metadata.step && std::isfinite(*property.metadata.step) &&
                    *property.metadata.step >= 0.0)
                    control.step = *property.metadata.step;
                break;
            case ReflectionValueType::String: control.role = ui::UiRole::TextInput; break;
            }
            switch (control.role)
            {
            case ui::UiRole::CheckBox: control.actions = {ui::UiActionKind::Toggle}; break;
            case ui::UiRole::NumericInput: control.actions = {ui::UiActionKind::Edit, ui::UiActionKind::Commit}; break;
            case ui::UiRole::TextInput: control.actions = {ui::UiActionKind::Edit, ui::UiActionKind::Commit, ui::UiActionKind::Cancel}; break;
            default: break;
            }
        }

        if (property.metadata.semantic == ReflectionWidgetSemantic::Enum &&
            control.choices.empty())
        {
            control.enabled = false;
            control.disabled_reason = "Enum metadata is unavailable";
            context.Text(control.disabled_reason);
            return {control.value, false, false, false};
        }
        std::string stable_name{section_key};
        stable_name.push_back('.');
        stable_name.append(property.key);
        const ui::UiControlDescriptor descriptor{
            .id = stable_name,
            .label = control.label,
            .description = control.description,
            .enabled = control.enabled,
            .disabled_reason = control.disabled_reason};
        switch (control.role)
        {
        case ui::UiRole::CheckBox:
            return ToEditResult(context.Checkbox(descriptor, std::get<bool>(control.value)));
        case ui::UiRole::NumericInput:
            return context.NumericInputValue(descriptor, control.value, control.minimum,
                                             control.maximum, control.step);
        case ui::UiRole::TextInput:
            return ToEditResult(context.TextInput(
                descriptor, std::get<std::string>(control.value)));
        case ui::UiRole::Combo:
            return context.ComboValue(descriptor, control.value, std::move(control.choices));
        default:
            return context.Control(control);
        }
    }
}
