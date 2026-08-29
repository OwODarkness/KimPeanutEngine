#include "render/light/light_world.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace
{
    bool IsFinite(const kpengine::Vector3f &value)
    {
        return std::isfinite(value.x_) && std::isfinite(value.y_) && std::isfinite(value.z_);
    }

    bool IsNonNegative(const kpengine::Vector3f &value)
    {
        return value.x_ >= 0.0f && value.y_ >= 0.0f && value.z_ >= 0.0f;
    }

    bool HasDirection(const kpengine::Vector3f &value)
    {
        return IsFinite(value) && value.SquareLength() > 0.0f;
    }
}

namespace kpengine::render
{
    bool IsLightDescValid(const LightDesc &desc)
    {
        if (!IsFinite(desc.color) || !IsNonNegative(desc.color) || !std::isfinite(desc.intensity) ||
            desc.intensity < 0.0f || (desc.shadow.has_value() && !desc.shadow->IsValid()))
        {
            return false;
        }

        switch (desc.type)
        {
        case LightType::Directional:
        {
            const auto *const directional = std::get_if<DirectionalLightData>(&desc.type_data);
            return directional != nullptr && HasDirection(directional->direction);
        }
        case LightType::Point:
        {
            const auto *const point = std::get_if<PointLightData>(&desc.type_data);
            return point != nullptr && IsFinite(point->position) && std::isfinite(point->range) &&
                   point->range > 0.0f;
        }
        case LightType::Spot:
        {
            const auto *const spot = std::get_if<SpotLightData>(&desc.type_data);
            return spot != nullptr && IsFinite(spot->position) && HasDirection(spot->direction) &&
                   std::isfinite(spot->range) && spot->range > 0.0f &&
                   std::isfinite(spot->inner_cone_radians) &&
                   std::isfinite(spot->outer_cone_radians) && spot->inner_cone_radians >= 0.0f &&
                   spot->outer_cone_radians >= spot->inner_cone_radians &&
                   spot->outer_cone_radians < 1.570796327f;
        }
        }
        return false;
    }

    bool IsShadowKindCompatible(LightType light_type, ShadowKind shadow_kind)
    {
        switch (light_type)
        {
        case LightType::Directional:
            return shadow_kind == ShadowKind::Directional2D;
        case LightType::Point:
            return shadow_kind == ShadowKind::PointCube;
        case LightType::Spot:
            return shadow_kind == ShadowKind::Spot2D;
        }
        return false;
    }

    bool IsShadowJobDescValid(const ShadowJobDesc &desc)
    {
        return desc.source_light.IsValid() && desc.resolution > 0;
    }

    LightHandle LightWorld::EnqueueCreate(const LightDesc &desc)
    {
        std::scoped_lock lock(mutex_);
        if (!IsLightDescValid(desc))
        {
            return {};
        }
        const LightHandle handle = handles_.Create();
        pending_commands_.push_back(CreateLightCommand{handle, desc});
        return handle;
    }

    bool LightWorld::EnqueueUpdate(LightHandle handle, const LightDesc &desc)
    {
        std::scoped_lock lock(mutex_);
        if (!handles_.IsHandleValid(handle) || !IsLightDescValid(desc))
        {
            return false;
        }
        pending_commands_.push_back(UpdateLightCommand{handle, desc});
        return true;
    }

    bool LightWorld::EnqueueDestroy(LightHandle handle)
    {
        std::scoped_lock lock(mutex_);
        if (!handles_.IsHandleValid(handle))
        {
            return false;
        }
        pending_commands_.push_back(DestroyLightCommand{handle});
        return true;
    }

    void LightWorld::ApplyPendingCommands()
    {
        std::scoped_lock lock(mutex_);
        for (const LightCommand &command : pending_commands_)
        {
            std::visit(
                [this](const auto &value)
                {
                    using Command = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Command, CreateLightCommand>)
                    {
                        if (handles_.IsHandleValid(value.handle) &&
                            lights_.find(value.handle.id) == lights_.end())
                        {
                            lights_.emplace(value.handle.id, Light{value.handle, value.desc});
                        }
                    }
                    else if constexpr (std::is_same_v<Command, UpdateLightCommand>)
                    {
                        const auto it = lights_.find(value.handle.id);
                        if (it != lights_.end() && it->second.handle == value.handle)
                        {
                            it->second.desc = value.desc;
                        }
                    }
                    else
                    {
                        const auto it = lights_.find(value.handle.id);
                        if (it != lights_.end() && it->second.handle == value.handle)
                        {
                            lights_.erase(it);
                            (void)handles_.Destroy(value.handle);
                        }
                    }
                },
                command);
        }
        pending_commands_.clear();
    }

    std::vector<Light> LightWorld::Snapshot() const
    {
        std::scoped_lock lock(mutex_);
        std::vector<Light> snapshot;
        snapshot.reserve(lights_.size());
        for (const auto &[id, light] : lights_)
        {
            (void)id;
            snapshot.push_back(light);
        }
        std::sort(snapshot.begin(), snapshot.end(),
                  [](const Light &lhs, const Light &rhs)
                  { return lhs.handle.id < rhs.handle.id; });
        return snapshot;
    }

    std::optional<Light> LightWorld::Find(LightHandle handle) const
    {
        std::scoped_lock lock(mutex_);
        const auto it = lights_.find(handle.id);
        return it != lights_.end() && it->second.handle == handle
                   ? std::optional<Light>{it->second}
                   : std::nullopt;
    }

    bool LightWorld::IsRegistered(LightHandle handle) const
    {
        std::scoped_lock lock(mutex_);
        return IsHandleRegistered(handle);
    }

    void LightWorld::Clear()
    {
        std::scoped_lock lock(mutex_);
        lights_.clear();
        pending_commands_.clear();
        handles_ = {};
    }

    bool LightWorld::IsHandleRegistered(LightHandle handle) const
    {
        const auto it = lights_.find(handle.id);
        return it != lights_.end() && it->second.handle == handle;
    }
}
