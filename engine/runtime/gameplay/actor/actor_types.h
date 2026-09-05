#ifndef KPENGINE_RUNTIME_GAMEPLAY_ACTOR_ACTOR_TYPES_H
#define KPENGINE_RUNTIME_GAMEPLAY_ACTOR_ACTOR_TYPES_H

#include <cstdint>

#include "base/handle.h"

namespace kpengine::gameplay
{
    struct ActorTag
    {
    };

    using ActorHandle = Handle<ActorTag>;

    struct ComponentInstanceId
    {
        uint32_t value = 0;

        constexpr bool IsValid() const noexcept { return value != 0; }

        friend constexpr bool operator==(ComponentInstanceId lhs,
                                         ComponentInstanceId rhs) noexcept
        {
            return lhs.value == rhs.value;
        }

        friend constexpr bool operator!=(ComponentInstanceId lhs,
                                         ComponentInstanceId rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    // An Actor initializes once, can activate/deactivate repeatedly, then
    // becomes permanently unavailable after destruction.
    enum class ActorState
    {
        Constructed,
        Initialized,
        Active,
        Inactive,
        Destroyed,
    };
}

#endif
