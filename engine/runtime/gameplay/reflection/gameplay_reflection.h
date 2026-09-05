#ifndef KPENGINE_RUNTIME_GAMEPLAY_REFLECTION_GAMEPLAY_REFLECTION_H
#define KPENGINE_RUNTIME_GAMEPLAY_REFLECTION_GAMEPLAY_REFLECTION_H

#include "reflection/reflection_types.h"

namespace kpengine::reflection
{
    class EnttReflectionRegistrar;
}

namespace kpengine::gameplay
{
    reflection::ReflectionResult RegisterGameplayReflection(
        reflection::EnttReflectionRegistrar &registrar);
}

#endif
