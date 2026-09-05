#include "gameplay/reflection/gameplay_reflection.h"

#include "gameplay/reflection/gameplay_reflection_internal.h"

namespace kpengine::gameplay
{
    reflection::ReflectionResult RegisterGameplayReflection(
        reflection::EnttReflectionRegistrar &registrar)
    {
        reflection::ReflectionResult result =
            reflection_detail::RegisterActorReflection(registrar);
        if (!result)
        {
            return result;
        }

        result = reflection_detail::RegisterLightReflection(registrar);
        if (!result)
        {
            return result;
        }

        return reflection_detail::RegisterCameraReflection(registrar);
    }
}
