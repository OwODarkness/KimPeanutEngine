#ifndef KPENGINE_RUNTIME_CAMERA_CONTROL_H
#define KPENGINE_RUNTIME_CAMERA_CONTROL_H

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
}

#endif
