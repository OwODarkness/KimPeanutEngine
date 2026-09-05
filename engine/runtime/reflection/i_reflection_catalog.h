#ifndef KPENGINE_RUNTIME_REFLECTION_I_REFLECTION_CATALOG_H
#define KPENGINE_RUNTIME_REFLECTION_I_REFLECTION_CATALOG_H

#include "reflection_types.h"

namespace kpengine::reflection
{
    class IReflectionCatalog
    {
    public:
        virtual ~IReflectionCatalog() = default;

        virtual const ReflectionTypeDescriptor *FindType(ReflectionTypeId type) const noexcept = 0;
        virtual const ReflectionTypeDescriptor *FindType(std::string_view name) const noexcept = 0;
        virtual const ReflectionPropertyDescriptor *FindProperty(
            ReflectionTypeId type,
            ReflectionPropertyId property) const noexcept = 0;
        virtual std::vector<ReflectionTypeDescriptor> EnumerateTypes() const = 0;
    };
}

#endif
