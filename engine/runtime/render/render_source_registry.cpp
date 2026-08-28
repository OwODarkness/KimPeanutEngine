#include "render/render_source_registry.h"

#include <type_traits>

namespace kpengine::render
{
    RenderableSourceHandle RenderableSourceRegistry::EnqueueCreate(
        const PrimitiveRenderableSourceDesc &source)
    {
        std::scoped_lock lock(command_mutex_);
        const RenderableSourceHandle handle = handles_.Create();
        pending_commands_.push_back(CreateCommand{handle, source});
        return handle;
    }

    bool RenderableSourceRegistry::EnqueueUpdate(
        RenderableSourceHandle handle, const PrimitiveRenderableSourceDesc &source)
    {
        std::scoped_lock lock(command_mutex_);
        if (!handles_.IsHandleValid(handle))
        {
            return false;
        }
        pending_commands_.push_back(UpdateCommand{handle, source});
        return true;
    }

    bool RenderableSourceRegistry::EnqueueDestroy(RenderableSourceHandle handle)
    {
        std::scoped_lock lock(command_mutex_);
        if (!handles_.Destroy(handle))
        {
            return false;
        }
        pending_commands_.push_back(DestroyCommand{handle});
        return true;
    }

    void RenderableSourceRegistry::Drain(RenderWorld &render_world, const ResolveFunction &resolve)
    {
        std::vector<Command> commands;
        {
            std::scoped_lock lock(command_mutex_);
            commands.swap(pending_commands_);
        }
        ApplyCommands(render_world, std::move(commands));
        ResolveRecords(render_world, resolve);
    }

    void RenderableSourceRegistry::Clear(RenderWorld &render_world)
    {
        std::vector<Command> commands;
        {
            std::scoped_lock lock(command_mutex_);
            commands.swap(pending_commands_);
            handles_ = {};
        }
        ApplyCommands(render_world, std::move(commands));
        for (auto &[id, record] : records_)
        {
            (void)id;
            RetireProxy(render_world, record);
        }
        records_.clear();
    }

    void RenderableSourceRegistry::ApplyCommands(RenderWorld &render_world,
                                                   std::vector<Command> commands)
    {
        for (const Command &command : commands)
        {
            std::visit(
                [this, &render_world](const auto &value)
                {
                    using CommandType = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<CommandType, CreateCommand>)
                    {
                        records_[value.handle.id] = {value.handle, value.source};
                    }
                    else if constexpr (std::is_same_v<CommandType, UpdateCommand>)
                    {
                        const auto it = records_.find(value.handle.id);
                        if (it != records_.end() && it->second.handle == value.handle)
                        {
                            it->second.source = value.source;
                            it->second.state = RenderableSourceState::Pending;
                            it->second.needs_resolution = true;
                            it->second.diagnostic.clear();
                        }
                    }
                    else
                    {
                        const auto it = records_.find(value.handle.id);
                        if (it != records_.end() && it->second.handle == value.handle)
                        {
                            RetireProxy(render_world, it->second);
                            records_.erase(it);
                        }
                    }
                },
                command);
        }
    }

    void RenderableSourceRegistry::ResolveRecords(RenderWorld &render_world,
                                                   const ResolveFunction &resolve)
    {
        for (auto &[id, record] : records_)
        {
            (void)id;
            if (!record.needs_resolution && record.state == RenderableSourceState::Ready)
            {
                continue;
            }
            const RenderableSourceResolution resolution = resolve(record.source);
            record.state = resolution.state;
            record.diagnostic = resolution.diagnostic;
            record.needs_resolution = resolution.state == RenderableSourceState::Pending;
            if (resolution.state != RenderableSourceState::Ready || !resolution.proxy_desc)
            {
                RetireProxy(render_world, record);
                continue;
            }
            if (record.proxy_handle.IsValid())
            {
                (void)render_world.EnqueueUpdate(record.proxy_handle, *resolution.proxy_desc);
            }
            else
            {
                record.proxy_handle = render_world.EnqueueCreate(*resolution.proxy_desc);
            }
        }
    }

    void RenderableSourceRegistry::RetireProxy(RenderWorld &render_world, SourceRecord &record)
    {
        if (record.proxy_handle.IsValid())
        {
            (void)render_world.EnqueueDestroy(record.proxy_handle);
            record.proxy_handle = {};
        }
    }
}
