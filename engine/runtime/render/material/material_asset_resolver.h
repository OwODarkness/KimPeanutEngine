#ifndef KPENGINE_RUNTIME_RENDER_MATERIAL_MATERIAL_ASSET_RESOLVER_H
#define KPENGINE_RUNTIME_RENDER_MATERIAL_MATERIAL_ASSET_RESOLVER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

#include "asset/common.h"
#include "render/material/material_system.h"
#include "render/prepared_render_asset_catalog.h"

namespace kpengine::asset
{
    struct MaterialResource;
}

namespace kpengine::render
{
    // Render-owned conversion from a CPU material asset to the shared template
    // and default instance that MeshProxy uses privately. Gameplay never sees
    // these handles; this cache is keyed solely by stable AssetID identity.
    class MaterialAssetResolver final
    {
    public:
        MaterialAssetResolver(MaterialSystem &material_system,
                              std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets);

        MaterialResolution Resolve(asset::AssetID material_asset,
                                   MaterialInstanceHandle &out_instance);
        void Clear();
        std::size_t GetRecordCount() const noexcept { return records_.size(); }

    private:
        struct Record
        {
            MaterialTemplateHandle template_handle;
            MaterialInstanceHandle default_instance;
        };

        bool BuildStandardPbrTemplate(asset::AssetID material_asset,
                                      const asset::MaterialResource &material,
                                      MaterialTemplateDesc &template_desc);

        MaterialSystem *material_system_ = nullptr;
        std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets_;
        std::unordered_map<uint64_t, Record> records_;
        asset::AssetID default_white_;
        asset::AssetID default_flat_normal_;
    };
}

#endif
