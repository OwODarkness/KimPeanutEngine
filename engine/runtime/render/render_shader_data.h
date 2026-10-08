#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SHADER_DATA_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SHADER_DATA_H

#include <cstddef>

#include "render_camera.h"

namespace kpengine::render
{
    struct PerPassData
    {
        CameraData camera_data;
        alignas(16) Matrix4f previous_view;
        alignas(16) Matrix4f previous_proj;
        alignas(16) Vector4f temporal_params;
        alignas(16) Vector4f history_params;
    };

    struct PerObjectData
    {
        Matrix4f model;
        Matrix4f previous_submitted_model;
        Vector4f temporal_state;
    };

    static_assert(sizeof(CameraData) == sizeof(Matrix4f) * 2);
    static_assert(alignof(PerPassData) == alignof(CameraData));
    static_assert(offsetof(PerPassData, camera_data) == 0);
    static_assert(offsetof(PerPassData, previous_view) == sizeof(CameraData));
    static_assert(offsetof(PerPassData, previous_proj) == sizeof(CameraData) + sizeof(Matrix4f));
    static_assert(offsetof(PerPassData, temporal_params) == sizeof(CameraData) + sizeof(Matrix4f) * 2);
    static_assert(offsetof(PerPassData, history_params) == sizeof(CameraData) + sizeof(Matrix4f) * 2 + sizeof(Vector4f));
    static_assert(sizeof(PerPassData) == sizeof(CameraData) + sizeof(Matrix4f) * 2 + sizeof(Vector4f) * 2);
    static_assert(alignof(PerObjectData) == alignof(Matrix4f));
    static_assert(offsetof(PerObjectData, model) == 0);
    static_assert(offsetof(PerObjectData, previous_submitted_model) == sizeof(Matrix4f));
    static_assert(offsetof(PerObjectData, temporal_state) == sizeof(Matrix4f) * 2);
    static_assert(sizeof(PerObjectData) == sizeof(Matrix4f) * 2 + sizeof(Vector4f));
}

#endif
