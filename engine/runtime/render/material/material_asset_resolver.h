#ifndef KPENGINE_RUNTIME_RENDER_MATERIAL_MATERIAL_ASSET_RESOLVER_H
#define KPENGINE_RUNTIME_RENDER_MATERIAL_MATERIAL_ASSET_RESOLVER_H

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "asset/common.h"
#include "render/material/material_system.h"

namespace kpengine::render
{
    // Render-owned conversion from a CPU material asset to the shared template
    // and default instance that MeshProxy uses privately. Gameplay never sees
    // these handles; this cache is keyed solely by stable AssetID identity.
    class MaterialAssetResolver final
    {
    public:
        explicit MaterialAssetResolver(MaterialSystem &material_system);

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

        MaterialSystem *material_system_ = nullptr;
        std::unordered_map<uint64_t, Record> records_;
    };
}

#endif
