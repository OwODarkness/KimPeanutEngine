#ifndef KPENGINE_RUNTIME_GAMEPLAY_REFLECTION_GAMEPLAY_REFLECTION_H
#define KPENGINE_RUNTIME_GAMEPLAY_REFLECTION_GAMEPLAY_REFLECTION_H

#include <string>
#include <vector>

#include "reflection/reflection_types.h"

namespace kpengine::reflection
{
    class EnttReflectionRegistrar;
}

namespace kpengine::gameplay
{
    class ActorComponent;

    struct GameplayReflectionBinding
    {
        using MatchFunction = bool (*)(const ActorComponent &) noexcept;
        using ConstObjectFunction = reflection::ReflectionObjectRef (*)(
            reflection::ReflectionTypeId, const ActorComponent &) noexcept;
        using MutableObjectFunction = reflection::ReflectionObjectRef (*)(
            reflection::ReflectionTypeId, ActorComponent *) noexcept;

        std::string canonical_name;
        MatchFunction matches = nullptr;
        ConstObjectFunction make_const_object = nullptr;
        MutableObjectFunction make_mutable_object = nullptr;
    };

    reflection::ReflectionResult RegisterGameplayReflection(
        reflection::EnttReflectionRegistrar &registrar);

    std::vector<GameplayReflectionBinding> GetGameplayReflectionBindings();
}

#endif
