#ifndef KPENGINE_RUNTIME_REFLECTION_I_REFLECTION_ACCESS_H
#define KPENGINE_RUNTIME_REFLECTION_I_REFLECTION_ACCESS_H

#include "reflection_types.h"

namespace kpengine::reflection
{
    class IReflectionAccess
    {
    public:
        virtual ~IReflectionAccess() = default;

        virtual ReflectionReadResult Read(const ReflectionObjectRef &object,
                                          ReflectionPropertyId property) const = 0;
        virtual ReflectionResult Write(const ReflectionObjectRef &object,
                                       ReflectionPropertyId property,
                                       const ReflectionValue &value) const = 0;
    };
}

#endif
