#ifndef KPENGINE_RUNTIME_CAMERA_CONTROL_H
#define KPENGINE_RUNTIME_CAMERA_CONTROL_H

#include <optional>

#include "gameplay/actor/actor_types.h"
#include "spatial/ray.h"

namespace kpengine::runtime
{
    // Render-thread/editor notification seam for the currently selected scene
    // camera. Implementations must only record the request; Runtime applies it
    // at the game-thread gameplay boundary.
    class ISceneCameraControlSink
    {
    public:
        virtual ~ISceneCameraControlSink() = default;
        virtual void SetSceneCameraControlCaptured(bool captured) = 0;
    };

    struct ScenePickResult
    {
        bool hit = false;
        gameplay::ActorHandle actor;
    };

    // Render-thread/editor to game-thread selection seam. The request is a
    // copied ray; Runtime resolves it against Gameplay-owned world identity.
    class ISceneSelectionSink
    {
    public:
        virtual ~ISceneSelectionSink() = default;
        virtual void EnqueueScenePick(const spatial::Ray &ray) = 0;
        virtual std::optional<ScenePickResult> ConsumeScenePickResult() = 0;
    };
}

#endif
