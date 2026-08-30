#ifndef KPENGINE_RUNTIME_RENDER_CAMERA_SOURCE_H
#define KPENGINE_RUNTIME_RENDER_CAMERA_SOURCE_H

#include <cstdint>

#include "base/handle.h"
#include "math/math_header.h"

namespace kpengine::render
{
    struct CameraSourceTag
    {
    };

    // Opaque source identity retained by Gameplay. It never identifies a
    // RenderCamera, matrix, GPU object, or backend resource.
    using CameraSourceHandle = Handle<CameraSourceTag>;

    enum class CameraProjectionMode : uint8_t
    {
        Perspective,
        Orthographic,
    };

    // Value-only camera authoring state. Render owns aspect ratio and derives
    // matrices from this descriptor at the frame boundary.
    struct CameraSourceDesc
    {
        Transform3f world_transform;
        CameraProjectionMode projection_mode = CameraProjectionMode::Perspective;
        float field_of_view_degrees = 45.0f;
        float near_plane = 0.1f;
        float far_plane = 2000.0f;
        float orthographic_height = 10.0f;
        bool enabled = true;
        int priority = 0;
    };

    // Public Gameplay -> Render camera boundary. Producers submit copied
    // values and retain only the opaque registration token.
    class ICameraSourceSink
    {
    public:
        virtual ~ICameraSourceSink() = default;

        virtual CameraSourceHandle EnqueueCreate(const CameraSourceDesc &source) = 0;
        virtual bool EnqueueUpdate(CameraSourceHandle handle,
                                   const CameraSourceDesc &source) = 0;
        virtual bool EnqueueDestroy(CameraSourceHandle handle) = 0;
    };
}

#endif
