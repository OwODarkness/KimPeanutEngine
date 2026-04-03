#ifndef KPENGINE_RUNTIME_ASSET_MODEL_LOADER_H
#define KPENGINE_RUNTIME_ASSET_MODEL_LOADER_H

#include <memory>
#include <string>

#include "common.h"
#include "asset.h"

namespace kpengine::asset{
    class IModelLoader{
    public:
        virtual bool Load(const std::string& path, ModelGeometryType type,  AssetRegisterInfo &info) = 0;
    };
}

#endif