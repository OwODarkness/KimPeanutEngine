#include "temporal_reprojection.h"

#include <algorithm>
#include <cstddef>
#include <cmath>

namespace kpengine::render
{
    namespace
    {
        constexpr float kMinimumClipW = 1.0e-6f;

        bool IsInsideClip(const Vector4f &clip, bool vulkan_clip_depth) noexcept
        {
            if (!std::isfinite(clip.x_) || !std::isfinite(clip.y_) ||
                !std::isfinite(clip.z_) || !std::isfinite(clip.w_) ||
                clip.w_ <= kMinimumClipW)
            {
                return false;
            }

            const float minimum_z = vulkan_clip_depth ? 0.0f : -clip.w_;
            return clip.z_ >= minimum_z && clip.z_ <= clip.w_;
        }

        Vector2f ToTopLeftUv(const Vector4f &clip) noexcept
        {
            const float inverse_w = 1.0f / clip.w_;
            return {(clip.x_ * inverse_w + 1.0f) * 0.5f,
                    (1.0f - clip.y_ * inverse_w) * 0.5f};
        }

        bool IsInsideUnitSquare(const Vector2f &uv) noexcept
        {
            return std::isfinite(uv.x_) && std::isfinite(uv.y_) &&
                   uv.x_ >= 0.0f && uv.x_ <= 1.0f &&
                   uv.y_ >= 0.0f && uv.y_ <= 1.0f;
        }
    }

    RasterMotionSample ComputeRasterMotionSample(
        const Vector4f &current_clip, const Vector4f &previous_clip,
        const Vector2f &current_jitter_uv, const Vector2f &previous_jitter_uv,
        float current_view_depth, bool previous_surface_valid,
        bool vulkan_clip_depth) noexcept
    {
        RasterMotionSample sample{};
        sample.view_depth = std::isfinite(current_view_depth)
                                ? std::max(0.0f, current_view_depth)
                                : 0.0f;
        if (!previous_surface_valid ||
            !IsInsideClip(current_clip, vulkan_clip_depth) ||
            !IsInsideClip(previous_clip, vulkan_clip_depth))
        {
            return sample;
        }

        const Vector2f current_uv = ToTopLeftUv(current_clip) - current_jitter_uv;
        const Vector2f previous_uv = ToTopLeftUv(previous_clip) - previous_jitter_uv;
        if (!IsInsideUnitSquare(current_uv) || !IsInsideUnitSquare(previous_uv))
        {
            return sample;
        }

        sample.motion_uv = previous_uv - current_uv;
        sample.history_valid = true;
        return sample;
    }

    bool IsRasterCameraCut(const CameraData &previous,
                           const CameraData &current) noexcept
    {
        constexpr float kProjectionCutThreshold = 0.05f;
        constexpr float kOrientationCutThreshold = 0.5f;
        constexpr float kTranslationCutThreshold = 10.0f;
        float orientation_delta = 0.0f;
        float translation_delta = 0.0f;
        float projection_delta = 0.0f;
        for (size_t row = 0; row < 4; ++row)
        {
            for (size_t column = 0; column < 4; ++column)
            {
                const float view_delta = std::abs(
                    previous.view[row][column] - current.view[row][column]);
                if (row < 3 && column < 3)
                {
                    orientation_delta = std::max(orientation_delta, view_delta);
                }
                else
                {
                    translation_delta = std::max(translation_delta, view_delta);
                }
                projection_delta = std::max(
                    projection_delta,
                    std::abs(previous.proj[row][column] - current.proj[row][column]));
            }
        }
        return orientation_delta > kOrientationCutThreshold ||
               translation_delta > kTranslationCutThreshold ||
               projection_delta > kProjectionCutThreshold;
    }
}
