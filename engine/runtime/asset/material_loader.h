#ifndef KPENGINE_RUNTIME_ASSET_MATERIAL_LOADER_H
#define KPENGINE_RUNTIME_ASSET_MATERIAL_LOADER_H

#include <string>

#include "asset.h"
#include "texture_variant_profile.h"

namespace kpengine::asset
{
    class MaterialLoader
    {
    public:
        void SetTextureVariantProfile(TextureVariantProfile profile) noexcept
        {
            texture_variant_profile_ = profile;
        }

        bool Load(const std::string &path, AssetRegisterInfo &info);

    private:
        TextureVariantProfile texture_variant_profile_ = TextureVariantProfile::Portable;
    };
}

#endif
