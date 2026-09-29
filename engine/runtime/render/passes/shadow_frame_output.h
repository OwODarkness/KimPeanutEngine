#ifndef KPENGINE_RUNTIME_RENDER_PASSES_SHADOW_FRAME_OUTPUT_H
#define KPENGINE_RUNTIME_RENDER_PASSES_SHADOW_FRAME_OUTPUT_H

#include <array>
#include <cstdint>
#include <vector>

#include "math/math_header.h"
#include "render/light/light_world.h"
#include "render/render_world/scene_visibility.h"

namespace kpengine::render
{
    struct DirectionalShadowFrame
    {
        ShadowJobDesc job;
        ShadowHandle shadow;
        uint64_t validity_stamp = 0;
        Vector3f light_direction;
        Matrix4f view;
        Matrix4f projection;
    };

    struct SpotShadowFrame
    {
        ShadowJobDesc job;
        ShadowHandle shadow;
        Vector3f position;
        Vector3f light_direction;
        float outer_cone_radians = 0.0f;
        float near_plane = 0.01f;
        float far_plane = 1.0f;
        Matrix4f view;
        Matrix4f projection;
    };

    struct PointShadowFrame
    {
        ShadowJobDesc job;
        ShadowHandle shadow;
        uint64_t validity_stamp = 0;
        Vector3f position;
        float near_plane = 0.01f;
        float far_plane = 1.0f;
        std::array<Matrix4f, 6> face_view_projections{};
        std::vector<VisibleMeshSection> caster_candidates;
    };
}

#endif
