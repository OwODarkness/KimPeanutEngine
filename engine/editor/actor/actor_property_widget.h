#ifndef KPENGINE_EDITOR_ACTOR_ACTOR_PROPERTY_WIDGET_H
#define KPENGINE_EDITOR_ACTOR_ACTOR_PROPERTY_WIDGET_H

#include <cstdint>
#include <string>

#include "gameplay/editor_bridge/gameplay_editor_bridge_types.h"
#include "reflection/reflection_types.h"

namespace kpengine::editor
{
    enum class ActorPropertyWidgetKind : uint8_t
    {
        ReadOnly,
        Checkbox,
        EnumCombo,
        SignedInteger,
        UnsignedInteger,
        FloatingPoint,
        String,
    };

    struct ActorPropertyWidgetPolicy
    {
        ActorPropertyWidgetKind kind = ActorPropertyWidgetKind::ReadOnly;
        bool editable = false;
        std::string diagnostic;
    };

    ActorPropertyWidgetPolicy ResolveActorPropertyWidget(
        const reflection::ReflectionPropertyDescriptor &descriptor,
        const gameplay::PropertyValueSnapshot &snapshot);

    std::string FormatReflectionValue(const reflection::ReflectionValue &value);
}

#endif
