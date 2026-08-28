#ifndef KPENGINE_RUNTIME_GAMEPLAY_ACTOR_ACTOR_TYPES_H
#define KPENGINE_RUNTIME_GAMEPLAY_ACTOR_ACTOR_TYPES_H

#include "base/handle.h"

namespace kpengine::gameplay
{
    struct ActorTag
    {
    };

    using ActorHandle = Handle<ActorTag>;

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
