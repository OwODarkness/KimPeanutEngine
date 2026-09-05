#include "editor/actor/actor_property_widget.h"

#include <iomanip>
#include <sstream>

namespace kpengine::editor
{
    ActorPropertyWidgetPolicy ResolveActorPropertyWidget(
        const reflection::ReflectionPropertyDescriptor &descriptor,
        const gameplay::PropertyValueSnapshot &snapshot)
    {
        ActorPropertyWidgetPolicy policy{};
        if (snapshot.status != reflection::ReflectionResultStatus::Success)
        {
            policy.diagnostic = snapshot.diagnostic.empty() ? "Property value is unavailable"
                                                             : snapshot.diagnostic;
            return policy;
        }
        if (descriptor.value_type != snapshot.value.GetType())
        {
            policy.diagnostic = "Reflected value type does not match its descriptor";
            return policy;
        }
        if (!reflection::HasFlag(descriptor.flags,
                                 reflection::ReflectionPropertyFlags::Readable))
        {
            policy.diagnostic = "Property is not readable";
            return policy;
        }
        if (!reflection::HasFlag(descriptor.flags,
                                 reflection::ReflectionPropertyFlags::EditorVisible))
        {
            policy.diagnostic = "Property is not visible in the editor";
            return policy;
        }

        policy.editable = reflection::HasFlag(descriptor.flags,
                                              reflection::ReflectionPropertyFlags::Writable);
        switch (descriptor.value_type)
        {
        case reflection::ReflectionValueType::Bool:
            policy.kind = ActorPropertyWidgetKind::Checkbox;
            break;
        case reflection::ReflectionValueType::SignedInteger:
            policy.kind = descriptor.metadata.semantic == reflection::ReflectionWidgetSemantic::Enum
                               ? ActorPropertyWidgetKind::EnumCombo
                               : ActorPropertyWidgetKind::SignedInteger;
            if (policy.kind == ActorPropertyWidgetKind::EnumCombo &&
                descriptor.metadata.enum_options.empty())
            {
                policy.editable = false;
                policy.diagnostic = "Enum property has no options";
            }
            break;
        case reflection::ReflectionValueType::UnsignedInteger:
            policy.kind = descriptor.metadata.semantic == reflection::ReflectionWidgetSemantic::Enum
                               ? ActorPropertyWidgetKind::EnumCombo
                               : ActorPropertyWidgetKind::UnsignedInteger;
            if (policy.kind == ActorPropertyWidgetKind::EnumCombo &&
                descriptor.metadata.enum_options.empty())
            {
                policy.editable = false;
                policy.diagnostic = "Enum property has no options";
            }
            break;
        case reflection::ReflectionValueType::FloatingPoint:
            policy.kind = ActorPropertyWidgetKind::FloatingPoint;
            break;
        case reflection::ReflectionValueType::String:
            policy.kind = ActorPropertyWidgetKind::String;
            break;
        }
        return policy;
    }

    std::string FormatReflectionValue(const reflection::ReflectionValue &value)
    {
        if (const bool *const boolean = value.TryGet<bool>())
        {
            return *boolean ? "true" : "false";
        }
        if (const int64_t *const signed_value = value.TryGet<int64_t>())
        {
            return std::to_string(*signed_value);
        }
        if (const uint64_t *const unsigned_value = value.TryGet<uint64_t>())
        {
            return std::to_string(*unsigned_value);
        }
        if (const double *const floating_value = value.TryGet<double>())
        {
            std::ostringstream stream;
            stream << std::setprecision(9) << *floating_value;
            return stream.str();
        }
        if (const std::string *const string_value = value.TryGet<std::string>())
        {
            return *string_value;
        }
        return "<invalid>";
    }
}
