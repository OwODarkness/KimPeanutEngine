#ifndef KPENGINE_RUNTIME_ASSET_MATERIAL_LOADER_H
#define KPENGINE_RUNTIME_ASSET_MATERIAL_LOADER_H

#include <atomic>
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
            texture_variant_profile_.store(profile, std::memory_order_release);
        }

        bool Load(const std::string &path, AssetRegisterInfo &info);

    private:
        std::atomic<TextureVariantProfile> texture_variant_profile_ =
            TextureVariantProfile::Portable;
    };
}

#endif
