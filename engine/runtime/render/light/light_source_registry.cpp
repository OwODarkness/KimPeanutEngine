#include "render/light/light_source_registry.h"

#include <type_traits>

namespace
{
    kpengine::render::LightDesc MakeLightDesc(
        const kpengine::render::DirectionalLightSourceDesc &source,
        std::optional<kpengine::render::ShadowHandle> shadow)
    {
        kpengine::render::LightDesc desc{};
        desc.type = kpengine::render::LightType::Directional;
        desc.color = source.color;
        desc.intensity = source.intensity;
        desc.enabled = source.enabled;
        desc.shadow = shadow;
        desc.type_data = kpengine::render::DirectionalLightData{source.direction};
        return desc;
    }

    kpengine::render::LightDesc MakeLightDesc(
        const kpengine::render::PointLightSourceDesc &source)
    {
        kpengine::render::LightDesc desc{};
        desc.type = kpengine::render::LightType::Point;
        desc.color = source.color;
        desc.intensity = source.intensity;
        desc.enabled = source.enabled;
        desc.type_data = kpengine::render::PointLightData{source.position, source.range};
        return desc;
    }

    kpengine::render::LightDesc MakeLightDesc(
        const kpengine::render::SpotLightSourceDesc &source)
    {
        kpengine::render::LightDesc desc{};
        desc.type = kpengine::render::LightType::Spot;
        desc.color = source.color;
        desc.intensity = source.intensity;
        desc.enabled = source.enabled;
        desc.type_data = kpengine::render::SpotLightData{
            source.position, source.direction, source.range, source.inner_cone_radians,
            source.outer_cone_radians};
        return desc;
    }
}

namespace kpengine::render
{
    LightSourceHandle LightSourceRegistry::EnqueueCreate(const LightSourceDesc &source)
    {
        std::scoped_lock lock(command_mutex_);
        const LightSourceHandle handle = handles_.Create();
        pending_commands_.push_back(CreateCommand{handle, source});
        return handle;
    }

    bool LightSourceRegistry::EnqueueUpdate(LightSourceHandle handle, const LightSourceDesc &source)
    {
        std::scoped_lock lock(command_mutex_);
        if (!handles_.IsHandleValid(handle))
        {
            return false;
        }
        pending_commands_.push_back(UpdateCommand{handle, source});
        return true;
    }

    bool LightSourceRegistry::EnqueueDestroy(LightSourceHandle handle)
    {
        std::scoped_lock lock(command_mutex_);
        if (!handles_.Destroy(handle))
        {
            return false;
        }
        pending_commands_.push_back(DestroyCommand{handle});
        return true;
    }

    void LightSourceRegistry::Drain(LightWorld &light_world)
    {
        std::vector<Command> commands;
        {
            std::scoped_lock lock(command_mutex_);
            commands.swap(pending_commands_);
        }
        ApplyCommands(light_world, std::move(commands));
        light_world.ApplyPendingCommands();
    }

    void LightSourceRegistry::Clear(LightWorld &light_world)
    {
        std::vector<Command> commands;
        {
            std::scoped_lock lock(command_mutex_);
            commands.swap(pending_commands_);
        }
        ApplyCommands(light_world, std::move(commands));
        for (const auto &[id, record] : records_)
        {
            (void)id;
            (void)light_world.EnqueueDestroy(record.light_handle);
        }
        records_.clear();
        light_world.ApplyPendingCommands();
        std::scoped_lock lock(command_mutex_);
        handles_ = {};
        shadow_handles_ = {};
    }

    void LightSourceRegistry::ApplyCommands(LightWorld &light_world, std::vector<Command> commands)
    {
        for (const Command &command : commands)
        {
            std::visit(
                [this, &light_world](const auto &value)
                {
                    using CommandType = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<CommandType, CreateCommand>)
                    {
                        const auto *const directional =
                            std::get_if<DirectionalLightSourceDesc>(&value.source);
                        if (directional != nullptr)
                        {
                            const std::optional<ShadowHandle> shadow =
                                directional->casts_shadow
                                    ? std::optional<ShadowHandle>{shadow_handles_.Create()}
                                    : std::nullopt;
                            const LightHandle light_handle =
                                light_world.EnqueueCreate(MakeLightDesc(*directional, shadow));
                            if (light_handle.IsValid())
                            {
                                records_[value.handle.id] = {value.handle, light_handle, shadow};
                            }
                            else if (shadow.has_value())
                            {
                                (void)shadow_handles_.Destroy(*shadow);
                            }
                        }
                        else if (const auto *const point =
                                     std::get_if<PointLightSourceDesc>(&value.source);
                                 point != nullptr)
                        {
                            const LightHandle light_handle =
                                light_world.EnqueueCreate(MakeLightDesc(*point));
                            if (light_handle.IsValid())
                            {
                                records_[value.handle.id] = {value.handle, light_handle, std::nullopt};
                            }
                        }
                        else if (const auto *const spot =
                                     std::get_if<SpotLightSourceDesc>(&value.source);
                                 spot != nullptr)
                        {
                            const LightHandle light_handle =
                                light_world.EnqueueCreate(MakeLightDesc(*spot));
                            if (light_handle.IsValid())
                            {
                                records_[value.handle.id] = {value.handle, light_handle, std::nullopt};
                            }
                        }
                    }
                    else if constexpr (std::is_same_v<CommandType, UpdateCommand>)
                    {
                        const auto record = records_.find(value.handle.id);
                        const auto *const directional =
                            std::get_if<DirectionalLightSourceDesc>(&value.source);
                        if (record != records_.end() && record->second.source_handle == value.handle &&
                            directional != nullptr)
                        {
                            if (directional->casts_shadow && !record->second.shadow_handle.has_value())
                            {
                                record->second.shadow_handle = shadow_handles_.Create();
                            }
                            else if (!directional->casts_shadow && record->second.shadow_handle.has_value())
                            {
                                (void)shadow_handles_.Destroy(*record->second.shadow_handle);
                                record->second.shadow_handle.reset();
                            }
                            (void)light_world.EnqueueUpdate(record->second.light_handle,
                                                            MakeLightDesc(*directional,
                                                                          record->second.shadow_handle));
                        }
                        else if (record != records_.end() &&
                                 record->second.source_handle == value.handle)
                        {
                            const auto *const point = std::get_if<PointLightSourceDesc>(&value.source);
                            if (point != nullptr && !record->second.shadow_handle.has_value())
                            {
                                (void)light_world.EnqueueUpdate(record->second.light_handle,
                                                                MakeLightDesc(*point));
                            }
                            else if (const auto *const spot =
                                         std::get_if<SpotLightSourceDesc>(&value.source);
                                     spot != nullptr && !record->second.shadow_handle.has_value())
                            {
                                (void)light_world.EnqueueUpdate(record->second.light_handle,
                                                                MakeLightDesc(*spot));
                            }
                        }
                    }
                    else
                    {
                        const auto record = records_.find(value.handle.id);
                        if (record != records_.end() && record->second.source_handle == value.handle)
                        {
                            (void)light_world.EnqueueDestroy(record->second.light_handle);
                            if (record->second.shadow_handle.has_value())
                            {
                                (void)shadow_handles_.Destroy(*record->second.shadow_handle);
                            }
                            records_.erase(record);
                        }
                    }
                },
                command);
        }
    }
}
