#include "render/light/light_gpu_data.h"

#include <algorithm>

namespace kpengine::render
{
    namespace
    {
        LightGpuShadowKind ToGpuShadowKind(ShadowKind kind)
        {
            switch (kind)
            {
            case ShadowKind::Directional2D:
                return LightGpuShadowKind::Directional2D;
            case ShadowKind::Spot2D:
                return LightGpuShadowKind::Spot2D;
            case ShadowKind::PointCube:
                return LightGpuShadowKind::PointCube;
            }
            return LightGpuShadowKind::Unshadowed;
        }
    }

    bool IsLightGpuFrameHeaderCompatible(const LightGpuFrameHeader &header)
    {
        return header.abi_version == kLightGpuDataAbiVersion &&
               header.light_count <= kMaxFrameLights &&
               header.light_stride == sizeof(LightGpuData);
    }

    std::optional<LightGpuData> EncodeLightGpuData(const LightDesc &light)
    {
        if (!IsLightDescValid(light))
        {
            return std::nullopt;
        }

        LightGpuData result{};
        result.color_intensity = Vector4f{light.color, light.intensity};
        result.layer_mask = light.layer_mask;
        result.enabled = light.enabled ? 1U : 0U;

        switch (light.type)
        {
        case LightType::Directional:
        {
            const auto &directional = std::get<DirectionalLightData>(light.type_data);
            result.direction_inner_cone =
                Vector4f{directional.direction.GetSafetyNormalize(), 0.0f};
            result.type = static_cast<uint32_t>(LightGpuType::Directional);
            break;
        }
        case LightType::Point:
        {
            const auto &point = std::get<PointLightData>(light.type_data);
            result.position_range = Vector4f{point.position, point.range};
            result.type = static_cast<uint32_t>(LightGpuType::Point);
            break;
        }
        case LightType::Spot:
        {
            const auto &spot = std::get<SpotLightData>(light.type_data);
            result.position_range = Vector4f{spot.position, spot.range};
            result.direction_inner_cone =
                Vector4f{spot.direction.GetSafetyNormalize(), spot.inner_cone_radians};
            result.outer_cone_radians = spot.outer_cone_radians;
            result.type = static_cast<uint32_t>(LightGpuType::Spot);
            break;
        }
        }
        return result;
    }

    LightGpuFrameData BuildLightGpuFrameData(
        const std::vector<Light> &lights,
        const std::optional<ResolvedLightShadowBinding> &resolved_shadow)
    {
        LightGpuFrameData result{};
        const size_t count = std::min(lights.size(), static_cast<size_t>(kMaxFrameLights));
        for (size_t index = 0; index < count; ++index)
        {
            const std::optional<LightGpuData> encoded = EncodeLightGpuData(lights[index].desc);
            if (encoded.has_value())
            {
                LightGpuData &gpu_light = result.lights[index];
                gpu_light = *encoded;
                if (resolved_shadow.has_value() &&
                    lights[index].handle == resolved_shadow->source_light &&
                    lights[index].desc.shadow.has_value() &&
                    *lights[index].desc.shadow == resolved_shadow->shadow &&
                    IsShadowKindCompatible(lights[index].desc.type, resolved_shadow->kind))
                {
                    gpu_light.shadow_kind =
                        static_cast<uint32_t>(ToGpuShadowKind(resolved_shadow->kind));
                    gpu_light.shadow_binding_slot = resolved_shadow->binding_slot;
                }
                ++result.header.light_count;
            }
        }
        return result;
    }
}
