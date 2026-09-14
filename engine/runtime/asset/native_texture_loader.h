#ifndef KPENGINE_RUNTIME_ASSET_NATIVE_TEXTURE_LOADER_H
#define KPENGINE_RUNTIME_ASSET_NATIVE_TEXTURE_LOADER_H

#include <atomic>
#include <cstdint>
#include <filesystem>

#include "asset.h"

namespace kpengine::asset
{
    class NativeTextureLoader final
    {
    public:
        explicit NativeTextureLoader(std::filesystem::path product_root = {});

        void SetInitialMipLevelCount(std::uint32_t mip_level_count) noexcept;
        bool Load(const std::string &path, AssetRegisterInfo &info) const;

    private:
        std::filesystem::path product_root_;
        std::atomic<std::uint32_t> initial_mip_level_count_{0};
    };
}

#endif
