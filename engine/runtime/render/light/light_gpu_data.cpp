#include "render/light/light_gpu_data.h"

#include <algorithm>

namespace kpengine::render
{
    namespace
    {
        void CopyVector3(std::array<float, 4> &destination, const Vector3f &source)
        {
            destination[0] = source.x_;
            destination[1] = source.y_;
            destination[2] = source.z_;
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
        CopyVector3(result.color_intensity, light.color);
        result.color_intensity[3] = light.intensity;
        result.layer_mask = light.layer_mask;
        result.enabled = light.enabled ? 1U : 0U;

        switch (light.type)
        {
        case LightType::Directional:
        {
            const auto &directional = std::get<DirectionalLightData>(light.type_data);
            CopyVector3(result.direction_inner_cone, directional.direction.GetSafetyNormalize());
            result.type = static_cast<uint32_t>(LightGpuType::Directional);
            break;
        }
        case LightType::Point:
        {
            const auto &point = std::get<PointLightData>(light.type_data);
            CopyVector3(result.position_range, point.position);
            result.position_range[3] = point.range;
            result.type = static_cast<uint32_t>(LightGpuType::Point);
            break;
        }
        case LightType::Spot:
        {
            const auto &spot = std::get<SpotLightData>(light.type_data);
            CopyVector3(result.position_range, spot.position);
            result.position_range[3] = spot.range;
            CopyVector3(result.direction_inner_cone, spot.direction.GetSafetyNormalize());
            result.direction_inner_cone[3] = spot.inner_cone_radians;
            result.outer_cone_radians = spot.outer_cone_radians;
            result.type = static_cast<uint32_t>(LightGpuType::Spot);
            break;
        }
        }
        return result;
    }

    LightGpuFrameData BuildLightGpuFrameData(const std::vector<Light> &lights)
    {
        LightGpuFrameData result{};
        const size_t count = std::min(lights.size(), static_cast<size_t>(kMaxFrameLights));
        for (size_t index = 0; index < count; ++index)
        {
            const std::optional<LightGpuData> encoded = EncodeLightGpuData(lights[index].desc);
            if (encoded.has_value())
            {
                result.lights[index] = *encoded;
                ++result.header.light_count;
            }
        }
        return result;
    }
}
