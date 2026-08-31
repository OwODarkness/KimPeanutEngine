#ifndef KPENGINE_RUNTIME_RENDER_LIGHT_SOURCE_H
#define KPENGINE_RUNTIME_RENDER_LIGHT_SOURCE_H

#include <variant>

#include "base/handle.h"
#include "math/math_header.h"

namespace kpengine::render
{
    struct LightSourceTag
    {
    };

    // Opaque Render-side registration token retained by Gameplay only to submit
    // later updates or destruction. It is not a LightWorld handle, GPU object,
    // shadow target, or descriptor.
    using LightSourceHandle = Handle<LightSourceTag>;

    // Gameplay authors only source values. Render resolves these to private
    // LightWorld records at the frame boundary.
    struct DirectionalLightSourceDesc
    {
        Vector3f direction{0.0f, -1.0f, 0.0f};
        Vector3f color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        bool enabled = true;
        // Authored intent only; Render resolves it to a private ShadowHandle.
        bool casts_shadow = true;
    };

    // D6.1 intentionally has no shadow request: point-shadow target lifetime
    // and scheduling remain a later Render-owned milestone.
    struct PointLightSourceDesc
    {
        Vector3f position{};
        Vector3f color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        float range = 1.0f;
        bool enabled = true;
    };

    // D6.2 retains punctual-shadow ownership in Render by exposing no shadow
    // request on the source; this light is deliberately unshadowed.
    struct SpotLightSourceDesc
    {
        Vector3f position{};
        Vector3f direction{0.0f, -1.0f, 0.0f};
        Vector3f color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        float range = 1.0f;
        float inner_cone_radians = 0.0f;
        float outer_cone_radians = 0.785398163f;
        bool enabled = true;
    };

    using LightSourceDesc =
        std::variant<DirectionalLightSourceDesc, PointLightSourceDesc, SpotLightSourceDesc>;

    // Public Gameplay -> Render source boundary. The later Render LightWorld
    // consumes copied values at a frame boundary; no Gameplay type is exposed
    // here and Gameplay cannot obtain a resolved LightHandle.
    class ILightSourceSink
    {
    public:
        virtual ~ILightSourceSink() = default;

        virtual LightSourceHandle EnqueueCreate(const LightSourceDesc &source) = 0;
        virtual bool EnqueueUpdate(LightSourceHandle handle, const LightSourceDesc &source) = 0;
        virtual bool EnqueueDestroy(LightSourceHandle handle) = 0;
    };
}

#endif
