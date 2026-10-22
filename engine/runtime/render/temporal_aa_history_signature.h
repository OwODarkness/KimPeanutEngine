#ifndef KPENGINE_RUNTIME_RENDER_TEMPORAL_AA_HISTORY_SIGNATURE_H
#define KPENGINE_RUNTIME_RENDER_TEMPORAL_AA_HISTORY_SIGNATURE_H

#include <bit>
#include <cstdint>
#include <type_traits>

#include "material/material_system.h"
#include "render_scene_coordinator.h"

namespace kpengine::render::detail
{
    namespace temporal_aa_history_signature
    {
        inline void HashCombine(uint64_t &hash, uint64_t value) noexcept
        {
            hash ^= value;
            hash *= 1099511628211ull;
        }

        inline void HashFloat(uint64_t &hash, float value) noexcept
        {
            HashCombine(hash, std::bit_cast<uint32_t>(value));
        }

        template <typename HandleType>
        inline void HashHandle(uint64_t &hash, HandleType handle) noexcept
        {
            HashCombine(hash, handle.id);
            HashCombine(hash, handle.generation);
        }
    }

    inline uint64_t ComputeTemporalAaSceneSignature(
        const RenderSceneFrameInput &input, const MaterialSystem &materials)
    {
        using namespace temporal_aa_history_signature;

        uint64_t hash = 1469598103934665603ull;
        HashCombine(hash, materials.GetRevision());
        for (const MeshProxy &proxy : input.render_world.Snapshot())
        {
            HashHandle(hash, proxy.handle);
            HashHandle(hash, proxy.mesh);
            HashHandle(hash, proxy.material);
            for (const MaterialInstanceHandle material : proxy.section_materials)
                HashHandle(hash, material);
        }
        for (const Light &light : input.lights)
        {
            HashHandle(hash, light.handle);
            HashCombine(hash, static_cast<uint64_t>(light.desc.type));
            HashFloat(hash, light.desc.color.x_);
            HashFloat(hash, light.desc.color.y_);
            HashFloat(hash, light.desc.color.z_);
            HashFloat(hash, light.desc.intensity);
            HashCombine(hash, light.desc.enabled ? 1u : 0u);
            std::visit([&hash](const auto &data) {
                using T = std::decay_t<decltype(data)>;
                if constexpr (std::is_same_v<T, DirectionalLightData>)
                {
                    HashFloat(hash, data.direction.x_);
                    HashFloat(hash, data.direction.y_);
                    HashFloat(hash, data.direction.z_);
                }
                else if constexpr (std::is_same_v<T, PointLightData>)
                {
                    HashFloat(hash, data.position.x_);
                    HashFloat(hash, data.position.y_);
                    HashFloat(hash, data.position.z_);
                    HashFloat(hash, data.range);
                }
                else
                {
                    HashFloat(hash, data.position.x_);
                    HashFloat(hash, data.position.y_);
                    HashFloat(hash, data.position.z_);
                    HashFloat(hash, data.direction.x_);
                    HashFloat(hash, data.direction.y_);
                    HashFloat(hash, data.direction.z_);
                    HashFloat(hash, data.range);
                    HashFloat(hash, data.inner_cone_radians);
                    HashFloat(hash, data.outer_cone_radians);
                }
            }, light.desc.type_data);
        }
        if (input.environment.has_value())
        {
            HashCombine(hash, input.environment->texture_asset.Pack());
            HashFloat(hash, input.environment->ibl_intensity);
        }
        if (input.environment_handle.has_value())
            HashHandle(hash, *input.environment_handle);
        return hash != 0 ? hash : 1;
    }
}

#endif
