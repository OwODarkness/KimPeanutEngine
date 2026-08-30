#include "render/camera_source_registry.h"

#include <cmath>
#include <type_traits>
#include <utility>

namespace kpengine::render
{
    namespace
    {
        bool IsFinite(const Vector3f &value)
        {
            return std::isfinite(value.x_) && std::isfinite(value.y_) &&
                   std::isfinite(value.z_);
        }

        bool IsFinite(const Rotatorf &value)
        {
            return std::isfinite(value.pitch_) && std::isfinite(value.yaw_) &&
                   std::isfinite(value.roll_);
        }

        bool IsValidSource(const CameraSourceDesc &source)
        {
            const bool valid_projection =
                source.projection_mode == CameraProjectionMode::Perspective ||
                source.projection_mode == CameraProjectionMode::Orthographic;
            return IsFinite(source.world_transform.position_) &&
                   IsFinite(source.world_transform.rotator_) &&
                   IsFinite(source.world_transform.scale_) &&
                   std::isfinite(source.field_of_view_degrees) &&
                   source.field_of_view_degrees >= 1.0f &&
                   source.field_of_view_degrees <= 179.0f &&
                   std::isfinite(source.near_plane) && source.near_plane >= 0.0001f &&
                   std::isfinite(source.far_plane) && source.far_plane > source.near_plane &&
                   std::isfinite(source.orthographic_height) &&
                   source.orthographic_height >= 0.0001f && valid_projection;
        }
    }

    CameraSourceHandle CameraSourceRegistry::EnqueueCreate(const CameraSourceDesc &source)
    {
        std::scoped_lock lock(command_mutex_);
        if (!IsValidSource(source))
        {
            return {};
        }
        const CameraSourceHandle handle = handles_.Create();
        pending_commands_.push_back(CreateCommand{handle, source});
        return handle;
    }

    bool CameraSourceRegistry::EnqueueUpdate(CameraSourceHandle handle,
                                             const CameraSourceDesc &source)
    {
        std::scoped_lock lock(command_mutex_);
        if (!handles_.IsHandleValid(handle) || !IsValidSource(source))
        {
            return false;
        }
        pending_commands_.push_back(UpdateCommand{handle, source});
        return true;
    }

    bool CameraSourceRegistry::EnqueueDestroy(CameraSourceHandle handle)
    {
        std::scoped_lock lock(command_mutex_);
        if (!handles_.Destroy(handle))
        {
            return false;
        }
        pending_commands_.push_back(DestroyCommand{handle});
        return true;
    }

    void CameraSourceRegistry::Drain()
    {
        std::vector<Command> commands;
        {
            std::scoped_lock lock(command_mutex_);
            commands.swap(pending_commands_);
        }
        ApplyCommands(std::move(commands));
        SelectActiveSource();
    }

    void CameraSourceRegistry::Clear()
    {
        {
            std::scoped_lock lock(command_mutex_);
            pending_commands_.clear();
            handles_ = {};
        }
        records_.clear();
        active_source_.reset();
    }

    void CameraSourceRegistry::ApplyCommands(std::vector<Command> commands)
    {
        for (const Command &command : commands)
        {
            std::visit(
                [this](const auto &value)
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
                        }
                    }
                    else
                    {
                        const auto it = records_.find(value.handle.id);
                        if (it != records_.end() && it->second.handle == value.handle)
                        {
                            records_.erase(it);
                        }
                    }
                },
                command);
        }
    }

    void CameraSourceRegistry::SelectActiveSource()
    {
        active_source_.reset();
        const SourceRecord *selected = nullptr;
        for (const auto &[id, record] : records_)
        {
            (void)id;
            if (!record.source.enabled ||
                (selected != nullptr &&
                 (record.source.priority < selected->source.priority ||
                  (record.source.priority == selected->source.priority &&
                   record.handle.id > selected->handle.id))))
            {
                continue;
            }
            selected = &record;
        }
        if (selected != nullptr)
        {
            active_source_ = selected->source;
        }
    }
}
