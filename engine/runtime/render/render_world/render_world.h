#ifndef KPENGINE_RUNTIME_RENDER_RENDER_WORLD_RENDER_WORLD_H
#define KPENGINE_RUNTIME_RENDER_RENDER_WORLD_RENDER_WORLD_H

#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "mesh_proxy.h"

namespace kpengine::render
{
    struct MeshProxyDesc
    {
        graphics::MeshHandle mesh;
        MaterialInstanceHandle material;
        Transform3f world_transform;
        spatial::AABB world_bounds{};
        RenderableFlags flags;
        int lod_bias = 0;
    };

    // Render-owned command boundary. The future World module produces only
    // these value types; it never mutates a MeshProxy after submission.
    struct CreateMeshProxyCommand
    {
        RenderableHandle handle;
        MeshProxyDesc desc;
    };

    struct UpdateMeshProxyCommand
    {
        RenderableHandle handle;
        MeshProxyDesc desc;
    };

    struct DestroyMeshProxyCommand
    {
        RenderableHandle handle;
    };

    using MeshProxyCommand = std::variant<CreateMeshProxyCommand, UpdateMeshProxyCommand,
                                          DestroyMeshProxyCommand>;

    // The registry is the sole owner of MeshProxy storage. Commands are
    // accepted from producers, then applied only at the render-frame boundary.
    class RenderWorld
    {
    public:
        RenderableHandle EnqueueCreate(const MeshProxyDesc &desc);
        bool EnqueueUpdate(RenderableHandle handle, const MeshProxyDesc &desc);
        bool EnqueueDestroy(RenderableHandle handle);

        void ApplyPendingCommands();
        std::vector<MeshProxy> Snapshot() const;
        std::optional<MeshProxy> Find(RenderableHandle handle) const;
        bool IsRegistered(RenderableHandle handle) const;
        void Clear();

    private:
        bool IsHandleRegistered(RenderableHandle handle) const;
        static MeshProxy MakeProxy(RenderableHandle handle, const MeshProxyDesc &desc);

        mutable std::mutex mutex_;
        HandleSystem<RenderableHandle> handles_;
        std::unordered_map<uint32_t, MeshProxy> proxies_;
        std::vector<MeshProxyCommand> pending_commands_;
    };
}

#endif
