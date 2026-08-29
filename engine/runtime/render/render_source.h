#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SOURCE_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SOURCE_H

#include <variant>

#include "asset/common.h"
#include "base/handle.h"
#include "math/math_header.h"
#include "spatial/aabb.h"

namespace kpengine::render
{
    struct RenderableSourceTag
    {
    };

    // Render assigns this generational token to a gameplay source record. It
    // identifies a registration request, never a MeshProxy or GPU object.
    using RenderableSourceHandle = Handle<RenderableSourceTag>;

    struct RenderableFlags
    {
        bool visible = true;
        bool opaque = true;
        bool casts_shadow = true;
    };

    // The only primitive source in the first gameplay slice. It describes a
    // static mesh; RenderSystem resolves it into a MeshProxy when ready.
    struct StaticMeshRenderableSourceDesc
    {
        asset::AssetID mesh_asset;
        // Gameplay selects serialized material identity only. Render resolves
        // the private template/default-instance pair.
        asset::AssetID material_asset;
        Transform3f world_transform;
        // Local bounds are the mesh-space bounds used when bootstrap creates a
        // Gameplay component. World bounds are the already-transformed bounds
        // consumed by Render visibility for this source snapshot.
        spatial::AABB local_bounds{};
        spatial::AABB world_bounds{};
        RenderableFlags flags;
        int lod_bias = 0;
    };

    using PrimitiveRenderableSourceDesc = std::variant<StaticMeshRenderableSourceDesc>;

    // Public Gameplay → Render boundary. Implemented later by RenderSystem;
    // gameplay submits copied logical source values and retains only this token.
    class IRenderableSourceSink
    {
    public:
        virtual ~IRenderableSourceSink() = default;

        virtual RenderableSourceHandle EnqueueCreate(
            const PrimitiveRenderableSourceDesc &source) = 0;
        virtual bool EnqueueUpdate(RenderableSourceHandle handle,
                                   const PrimitiveRenderableSourceDesc &source) = 0;
        virtual bool EnqueueDestroy(RenderableSourceHandle handle) = 0;
    };
}

#endif
