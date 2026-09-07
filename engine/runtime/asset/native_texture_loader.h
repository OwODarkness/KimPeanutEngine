#ifndef KPENGINE_RUNTIME_ASSET_NATIVE_TEXTURE_LOADER_H
#define KPENGINE_RUNTIME_ASSET_NATIVE_TEXTURE_LOADER_H

#include <filesystem>

#include "asset.h"

namespace kpengine::asset
{
    class NativeTextureLoader final
    {
    public:
        explicit NativeTextureLoader(std::filesystem::path product_root = {});

        bool Load(const std::string &path, AssetRegisterInfo &info) const;

    private:
        std::filesystem::path product_root_;
    };
}

#endif
