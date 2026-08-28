#ifndef KPENGINE_RUNTIME_GAMEPLAY_FACTORY_STATIC_MESH_ACTOR_FACTORY_H
#define KPENGINE_RUNTIME_GAMEPLAY_FACTORY_STATIC_MESH_ACTOR_FACTORY_H

#include "asset/common.h"
#include "gameplay/actor/actor_types.h"
#include "render/render_source.h"
#include "spatial/aabb.h"

namespace kpengine::gameplay
{
    class GameplayWorld;

    // Value-only authoring input for the first concrete Actor factory.
    struct StaticMeshActorDesc
    {
        asset::AssetID mesh_asset;
        asset::AssetID material_asset;
        Transform3f transform;
        spatial::AABB local_bounds{};
        bool visible = true;
        bool casts_shadow = true;
        int lod_bias = 0;
    };

    // Constructs and activates one standard Actor composition. GameplayWorld
    // remains responsible only for Actor ownership and lifecycle operations.
    ActorHandle CreateStaticMeshActor(GameplayWorld &world, const StaticMeshActorDesc &desc);
}

#endif
