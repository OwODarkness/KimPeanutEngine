#ifndef KPENGINE_RUNTIME_ASSET_LEVEL_LOADER_H
#define KPENGINE_RUNTIME_ASSET_LEVEL_LOADER_H

#include <filesystem>
#include <string>

#include "asset.h"

namespace kpengine::asset
{
    class LevelLoader
    {
    public:
        explicit LevelLoader(std::filesystem::path archive_root = {});

        bool Load(const std::string &path, AssetRegisterInfo &info);

    private:
        std::filesystem::path archive_root_;
    };
}

#endif
