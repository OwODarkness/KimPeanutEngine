#include "gameplay/component/mesh_component.h"

#include "gameplay/actor/actor.h"

namespace kpengine::gameplay
{
    void MeshComponent::SetMeshAsset(const asset::AssetID &mesh_asset)
    {
        if (!(mesh_asset_ == mesh_asset))
        {
            mesh_asset_ = mesh_asset;
            MarkSourceDirty();
        }
    }

    void MeshComponent::SetMaterialAsset(const asset::AssetID &material_asset)
    {
        if (!(material_asset_ == material_asset))
        {
            material_asset_ = material_asset;
            MarkSourceDirty();
        }
    }

    void MeshComponent::SetLodBias(int lod_bias)
    {
        if (lod_bias_ != lod_bias)
        {
            lod_bias_ = lod_bias;
            MarkSourceDirty();
        }
    }

    void MeshComponent::OnActivate()
    {
        SceneComponent::OnActivate();

        Actor *const owner = GetOwner();
        render::IRenderableSourceSink *const source_sink =
            owner != nullptr ? owner->GetRenderableSourceSink() : nullptr;
        if (source_sink != nullptr)
        {
            source_handle_ = source_sink->EnqueueCreate(BuildSourceDesc());
        }
        source_dirty_ = false;
    }

    void MeshComponent::OnDeactivate()
    {
        Actor *const owner = GetOwner();
        render::IRenderableSourceSink *const source_sink =
            owner != nullptr ? owner->GetRenderableSourceSink() : nullptr;
        if (source_sink != nullptr && source_handle_.IsValid())
        {
            (void)source_sink->EnqueueDestroy(source_handle_);
        }
        source_handle_ = {};
        source_dirty_ = true;
        SceneComponent::OnDeactivate();
    }

    void MeshComponent::OnTick(float delta_time)
    {
        SceneComponent::OnTick(delta_time);
        FlushSourceUpdate();
    }

    void MeshComponent::OnTransformChanged()
    {
        MarkSourceDirty();
    }

    void MeshComponent::OnPrimitiveStateChanged()
    {
        MarkSourceDirty();
    }

    render::PrimitiveRenderableSourceDesc MeshComponent::BuildSourceDesc() const
    {
        render::StaticMeshRenderableSourceDesc source{};
        source.mesh_asset = mesh_asset_;
        source.material_asset = material_asset_;
        source.world_transform = GetWorldTransform();
        source.local_bounds = GetLocalBounds();
        source.world_bounds = GetWorldBounds();
        source.flags.visible = IsVisible();
        source.flags.casts_shadow = CastsShadow();
        source.lod_bias = lod_bias_;
        return source;
    }

    void MeshComponent::MarkSourceDirty()
    {
        source_dirty_ = true;
    }

    void MeshComponent::FlushSourceUpdate()
    {
        if (!source_dirty_ || !source_handle_.IsValid())
        {
            return;
        }

        Actor *const owner = GetOwner();
        render::IRenderableSourceSink *const source_sink =
            owner != nullptr ? owner->GetRenderableSourceSink() : nullptr;
        if (source_sink != nullptr)
        {
            (void)source_sink->EnqueueUpdate(source_handle_, BuildSourceDesc());
        }
        source_dirty_ = false;
    }
}
