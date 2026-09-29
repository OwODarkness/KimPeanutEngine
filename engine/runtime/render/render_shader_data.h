#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SHADER_DATA_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SHADER_DATA_H

#include <cstddef>

#include "render_camera.h"

namespace kpengine::render
{
    struct PerPassData
    {
        CameraData camera_data;
    };

    struct PerObjectData
    {
        Matrix4f model;
    };

    static_assert(sizeof(CameraData) == sizeof(Matrix4f) * 2);
    static_assert(alignof(PerPassData) == alignof(CameraData));
    static_assert(offsetof(PerPassData, camera_data) == 0);
    static_assert(sizeof(PerPassData) == sizeof(CameraData));
    static_assert(alignof(PerObjectData) == alignof(Matrix4f));
    static_assert(offsetof(PerObjectData, model) == 0);
    static_assert(sizeof(PerObjectData) == sizeof(Matrix4f));
}

#endif
