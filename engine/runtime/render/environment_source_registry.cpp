#include "render/environment_source_registry.h"

#include <cmath>
#include <type_traits>
#include <utility>

namespace kpengine::render
{
    bool IsEnvironmentSourceDescValid(const EnvironmentSourceDesc &source)
    {
        return source.texture_asset.IsValid() &&
               source.texture_asset.type == asset::AssetType::KPAT_Texture &&
               std::isfinite(source.ibl_intensity) && source.ibl_intensity >= 0.0f;
    }

    EnvironmentSourceHandle EnvironmentSourceRegistry::EnqueueCreate(
        const EnvironmentSourceDesc &source)
    {
        std::scoped_lock lock(command_mutex_);
        if (live_handle_.has_value() || !IsEnvironmentSourceDescValid(source))
        {
            return {};
        }

        const EnvironmentSourceHandle handle = handles_.Create();
        try
        {
            pending_commands_.push_back(CreateCommand{handle, source});
        }
        catch (...)
        {
            (void)handles_.Destroy(handle);
            throw;
        }
        live_handle_ = handle;
        return handle;
    }

    bool EnvironmentSourceRegistry::EnqueueDestroy(EnvironmentSourceHandle handle)
    {
        std::scoped_lock lock(command_mutex_);
        if (!live_handle_.has_value() || !(*live_handle_ == handle) ||
            !handles_.IsHandleValid(handle))
        {
            return false;
        }

        bool command_enqueued = false;
        try
        {
            pending_commands_.push_back(DestroyCommand{handle});
            command_enqueued = true;
            (void)handles_.Destroy(handle);
        }
        catch (...)
        {
            if (command_enqueued)
            {
                pending_commands_.pop_back();
            }
            throw;
        }
        live_handle_.reset();
        return true;
    }

    void EnvironmentSourceRegistry::Drain()
    {
        std::vector<Command> commands;
        {
            std::scoped_lock lock(command_mutex_);
            commands.swap(pending_commands_);
        }
        ApplyCommands(std::move(commands));
    }

    void EnvironmentSourceRegistry::Clear()
    {
        {
            std::scoped_lock lock(command_mutex_);
            pending_commands_.clear();
            if (live_handle_.has_value())
            {
                (void)handles_.Destroy(*live_handle_);
            }
            live_handle_.reset();
        }
        active_source_.reset();
        active_handle_.reset();
    }

    void EnvironmentSourceRegistry::ApplyCommands(std::vector<Command> commands)
    {
        for (const Command &command : commands)
        {
            std::visit(
                [this](const auto &value)
                {
                    using CommandType = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<CommandType, CreateCommand>)
                    {
                        active_source_ = value.source;
                        active_handle_ = value.handle;
                    }
                    else
                    {
                        if (active_handle_.has_value() && *active_handle_ == value.handle)
                        {
                            active_source_.reset();
                            active_handle_.reset();
                        }
                    }
                },
                command);
        }
    }
}
