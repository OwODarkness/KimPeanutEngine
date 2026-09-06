#ifndef KPENGINE_RUNTIME_ASSET_NATIVE_MODEL_LOADER_H
#define KPENGINE_RUNTIME_ASSET_NATIVE_MODEL_LOADER_H

#include <filesystem>

#include "model_loader.h"

namespace kpengine::asset
{
    class NativeModelLoader final : public IModelLoader
    {
    public:
        explicit NativeModelLoader(std::filesystem::path product_root = {});

        bool Load(const std::string &path, ModelGeometryType type, AssetRegisterInfo &info) override;

    private:
        std::filesystem::path product_root_;
    };
}

#endif
