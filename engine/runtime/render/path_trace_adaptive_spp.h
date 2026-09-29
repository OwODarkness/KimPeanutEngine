#ifndef KPENGINE_RUNTIME_RENDER_PATH_TRACE_ADAPTIVE_SPP_H
#define KPENGINE_RUNTIME_RENDER_PATH_TRACE_ADAPTIVE_SPP_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "path_trace_settings.h"

namespace kpengine::render::detail
{
    struct PathTraceCameraMotionSample
    {
        std::array<float, 3> position{};
        std::array<float, 3> rotation_degrees{};
    };

    struct PathTraceAdaptiveSppState
    {
        bool initialized = false;
        bool camera_moving = true;
        uint32_t stable_frames = 0;
        float accumulated_translation = 0.0f;
        float accumulated_rotation_degrees = 0.0f;
        PathTraceCameraMotionSample last_submitted_camera{};
    };

    struct PathTraceAdaptiveSppDecision
    {
        bool camera_moving = true;
        uint32_t stable_frames = 0;
        PathTraceAdaptiveSppState next_state{};
    };

    enum class PathTraceAdaptiveSppPhase : uint8_t
    {
        Moving,
        ConvergingHighSpp,
        ConvergingMediumSpp,
        QualityMaintenance,
    };

    struct PathTraceAdaptiveSppSelection
    {
        PathTraceAdaptiveSppPhase phase = PathTraceAdaptiveSppPhase::Moving;
        uint32_t samples_per_dispatch = 1;
    };

    inline constexpr PathTraceReconstruction SelectAdaptivePathTraceReconstruction(
        const bool camera_moving,
        const PathTraceReconstruction moving_reconstruction) noexcept
    {
        return camera_moving ? moving_reconstruction : PathTraceReconstruction::Raw;
    }

    inline PathTraceAdaptiveSppSelection SelectPathTraceAdaptiveSpp(
        const bool camera_moving, const uint32_t accumulated_samples,
        const uint32_t quality_2spp_threshold, const uint32_t quality_1spp_threshold,
        const uint32_t moving_samples, const uint32_t settled_samples,
        const uint32_t medium_samples, const uint32_t quality_maintenance_samples) noexcept
    {
        if (camera_moving)
        {
            return {PathTraceAdaptiveSppPhase::Moving, moving_samples};
        }
        if (accumulated_samples >= quality_1spp_threshold)
        {
            return {PathTraceAdaptiveSppPhase::QualityMaintenance,
                    quality_maintenance_samples};
        }
        if (accumulated_samples >= quality_2spp_threshold)
        {
            return {PathTraceAdaptiveSppPhase::ConvergingMediumSpp, medium_samples};
        }
        return {PathTraceAdaptiveSppPhase::ConvergingHighSpp, settled_samples};
    }

    inline float ShortestAngleDeltaDegrees(const float from, const float to) noexcept
    {
        return std::abs(std::remainder(to - from, 360.0f));
    }

    inline PathTraceAdaptiveSppDecision EvaluatePathTraceAdaptiveSpp(
        const PathTraceAdaptiveSppState &state,
        const PathTraceCameraMotionSample &camera,
        const float translation_threshold,
        const float rotation_threshold_degrees,
        const uint32_t settle_frame_threshold) noexcept
    {
        PathTraceAdaptiveSppDecision decision{};
        decision.next_state = state;
        if (!state.initialized)
        {
            decision.next_state.initialized = true;
            decision.next_state.camera_moving = true;
            decision.next_state.stable_frames = 0;
            decision.next_state.last_submitted_camera = camera;
            decision.camera_moving = true;
            return decision;
        }

        const PathTraceCameraMotionSample &previous = state.last_submitted_camera;
        float translation_delta_squared = 0.0f;
        float rotation_delta = 0.0f;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const float delta = camera.position[axis] - previous.position[axis];
            translation_delta_squared += delta * delta;
            rotation_delta = std::max(rotation_delta, ShortestAngleDeltaDegrees(
                previous.rotation_degrees[axis], camera.rotation_degrees[axis]));
        }
        const float translation_delta = std::sqrt(translation_delta_squared);
        decision.next_state.accumulated_translation += translation_delta;
        decision.next_state.accumulated_rotation_degrees += rotation_delta;
        const bool motion_threshold_crossed =
            translation_delta >= translation_threshold ||
            rotation_delta >= rotation_threshold_degrees ||
            decision.next_state.accumulated_translation >= translation_threshold ||
            decision.next_state.accumulated_rotation_degrees >= rotation_threshold_degrees;
        if (motion_threshold_crossed)
        {
            decision.next_state.camera_moving = true;
            decision.next_state.stable_frames = 0;
            decision.next_state.accumulated_translation = 0.0f;
            decision.next_state.accumulated_rotation_degrees = 0.0f;
        }
        else if (state.camera_moving)
        {
            decision.next_state.stable_frames = std::min(
                state.stable_frames + 1u, settle_frame_threshold);
            decision.next_state.camera_moving =
                decision.next_state.stable_frames < settle_frame_threshold;
            if (!decision.next_state.camera_moving)
            {
                decision.next_state.accumulated_translation = 0.0f;
                decision.next_state.accumulated_rotation_degrees = 0.0f;
            }
        }
        else
        {
            decision.next_state.camera_moving = false;
            decision.next_state.stable_frames = settle_frame_threshold;
        }
        decision.next_state.last_submitted_camera = camera;
        decision.camera_moving = decision.next_state.camera_moving;
        decision.stable_frames = decision.next_state.stable_frames;
        return decision;
    }
}

#endif
