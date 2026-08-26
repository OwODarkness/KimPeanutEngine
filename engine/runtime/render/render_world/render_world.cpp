#include "render_world.h"

#include <algorithm>
#include <type_traits>

namespace kpengine::render
{
    RenderableHandle RenderWorld::EnqueueCreate(const MeshProxyDesc &desc)
    {
        std::scoped_lock lock(mutex_);
        const RenderableHandle handle = handles_.Create();
        pending_commands_.push_back({CreateMeshProxyCommand{handle, desc}});
        return handle;
    }

    bool RenderWorld::EnqueueUpdate(RenderableHandle handle, const MeshProxyDesc &desc)
    {
        std::scoped_lock lock(mutex_);
        if (!handles_.IsHandleValid(handle))
        {
            return false;
        }
        pending_commands_.push_back({UpdateMeshProxyCommand{handle, desc}});
        return true;
    }

    bool RenderWorld::EnqueueDestroy(RenderableHandle handle)
    {
        std::scoped_lock lock(mutex_);
        if (!handles_.IsHandleValid(handle))
        {
            return false;
        }
        pending_commands_.push_back({DestroyMeshProxyCommand{handle}});
        return true;
    }

    void RenderWorld::ApplyPendingCommands()
    {
        std::scoped_lock lock(mutex_);
        for (const MeshProxyCommand &command : pending_commands_)
        {
            std::visit(
                [this](const auto &value)
                {
                    using Command = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Command, CreateMeshProxyCommand>)
                    {
                        if (handles_.IsHandleValid(value.handle) &&
                            proxies_.find(value.handle.id) == proxies_.end())
                        {
                            proxies_.emplace(value.handle.id, MakeProxy(value.handle, value.desc));
                        }
                    }
                    else if constexpr (std::is_same_v<Command, UpdateMeshProxyCommand>)
                    {
                        const auto it = proxies_.find(value.handle.id);
                        if (it != proxies_.end() && it->second.handle == value.handle)
                        {
                            it->second = MakeProxy(value.handle, value.desc);
                        }
                    }
                    else
                    {
                        const auto it = proxies_.find(value.handle.id);
                        if (it != proxies_.end() && it->second.handle == value.handle)
                        {
                            proxies_.erase(it);
                            handles_.Destroy(value.handle);
                        }
                    }
                },
                command);
        }
        pending_commands_.clear();
    }

    std::vector<MeshProxy> RenderWorld::Snapshot() const
    {
        std::scoped_lock lock(mutex_);
        std::vector<MeshProxy> snapshot;
        snapshot.reserve(proxies_.size());
        for (const auto &[id, proxy] : proxies_)
        {
            (void)id;
            snapshot.push_back(proxy);
        }
        std::sort(snapshot.begin(), snapshot.end(),
                  [](const MeshProxy &lhs, const MeshProxy &rhs)
                  { return lhs.handle.id < rhs.handle.id; });
        return snapshot;
    }

    std::optional<MeshProxy> RenderWorld::Find(RenderableHandle handle) const
    {
        std::scoped_lock lock(mutex_);
        const auto it = proxies_.find(handle.id);
        return it != proxies_.end() && it->second.handle == handle
                   ? std::optional<MeshProxy>{it->second}
                   : std::nullopt;
    }

    bool RenderWorld::IsRegistered(RenderableHandle handle) const
    {
        std::scoped_lock lock(mutex_);
        return IsHandleRegistered(handle);
    }

    void RenderWorld::Clear()
    {
        std::scoped_lock lock(mutex_);
        proxies_.clear();
        pending_commands_.clear();
        handles_ = {};
    }

    bool RenderWorld::IsHandleRegistered(RenderableHandle handle) const
    {
        const auto it = proxies_.find(handle.id);
        return it != proxies_.end() && it->second.handle == handle;
    }

    MeshProxy RenderWorld::MakeProxy(RenderableHandle handle, const MeshProxyDesc &desc)
    {
        return {handle, desc.mesh, desc.material, desc.world_transform, desc.world_bounds,
                desc.flags, desc.lod_bias};
    }
}
