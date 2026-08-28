#include "gameplay/factory/static_mesh_actor_factory.h"

#include "gameplay/actor/actor.h"
#include "gameplay/component/mesh_component.h"
#include "gameplay/world/gameplay_world.h"

namespace kpengine::gameplay
{
    ActorHandle CreateStaticMeshActor(GameplayWorld &world, const StaticMeshActorDesc &desc)
    {
        if (!desc.mesh_asset.IsValid() || desc.mesh_asset.type != asset::AssetType::KPAT_Mesh ||
            !desc.material_asset.IsValid() ||
            desc.material_asset.type != asset::AssetType::KPAT_Material || !desc.local_bounds.IsValid())
        {
            return {};
        }

        const ActorHandle handle = world.CreateActor();
        Actor *const actor = world.FindActor(handle);
        MeshComponent *const mesh = actor ? actor->AddComponent<MeshComponent>() : nullptr;
        if (!mesh || !actor->SetRootComponent(mesh))
        {
            (void)world.DestroyActor(handle);
            return {};
        }

        mesh->SetMeshAsset(desc.mesh_asset);
        mesh->SetMaterialAsset(desc.material_asset);
        mesh->SetLocalTransform(desc.transform);
        mesh->SetLocalBounds(desc.local_bounds);
        mesh->SetVisible(desc.visible);
        mesh->SetCastsShadow(desc.casts_shadow);
        mesh->SetLodBias(desc.lod_bias);

        if (!world.InitializeActor(handle) || !world.ActivateActor(handle))
        {
            (void)world.DestroyActor(handle);
            return {};
        }
        return handle;
    }
}
