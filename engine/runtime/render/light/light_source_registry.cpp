#include "render/light/light_source_registry.h"

#include <type_traits>

namespace
{
    kpengine::render::LightDesc MakeLightDesc(
        const kpengine::render::DirectionalLightSourceDesc &source)
    {
        kpengine::render::LightDesc desc{};
        desc.type = kpengine::render::LightType::Directional;
        desc.color = source.color;
        desc.intensity = source.intensity;
        desc.enabled = source.enabled;
        desc.type_data = kpengine::render::DirectionalLightData{source.direction};
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
                            const LightHandle light_handle =
                                light_world.EnqueueCreate(MakeLightDesc(*directional));
                            if (light_handle.IsValid())
                            {
                                records_[value.handle.id] = {value.handle, light_handle};
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
                            (void)light_world.EnqueueUpdate(record->second.light_handle,
                                                            MakeLightDesc(*directional));
                        }
                    }
                    else
                    {
                        const auto record = records_.find(value.handle.id);
                        if (record != records_.end() && record->second.source_handle == value.handle)
                        {
                            (void)light_world.EnqueueDestroy(record->second.light_handle);
                            records_.erase(record);
                        }
                    }
                },
                command);
        }
    }
}
