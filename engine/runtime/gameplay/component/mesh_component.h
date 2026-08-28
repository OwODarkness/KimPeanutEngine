#ifndef KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_MESH_COMPONENT_H
#define KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_MESH_COMPONENT_H

#include "asset/common.h"
#include "gameplay/component/primitive_component.h"
#include "render/render_source.h"

namespace kpengine::gameplay
{
    // Gameplay-side static-mesh state. The source handle identifies only a
    // Render-owned registration; it is never a proxy or GPU resource.
    class MeshComponent  : public PrimitiveComponent
    {
    public:
        const asset::AssetID &GetMeshAsset() const { return mesh_asset_; }
        const asset::AssetID &GetMaterialAsset() const { return material_asset_; }
        int GetLodBias() const { return lod_bias_; }
        render::RenderableSourceHandle GetSourceHandle() const { return source_handle_; }

        void SetMeshAsset(const asset::AssetID &mesh_asset);
        void SetMaterialAsset(const asset::AssetID &material_asset);
        void SetLodBias(int lod_bias);

    protected:
        void OnActivate() override;
        void OnDeactivate() override;
        void OnTick(float delta_time) override;
        void OnTransformChanged() override;
        void OnPrimitiveStateChanged() override;

    private:
        render::PrimitiveRenderableSourceDesc BuildSourceDesc() const;
        void MarkSourceDirty();
        void FlushSourceUpdate();

        asset::AssetID mesh_asset_;
        asset::AssetID material_asset_;
        int lod_bias_ = 0;
        bool source_dirty_ = true;
        render::RenderableSourceHandle source_handle_;
    };
}

#endif
