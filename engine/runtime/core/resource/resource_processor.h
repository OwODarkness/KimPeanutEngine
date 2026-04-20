#ifndef KPENGINE_RUNTIME_RESOURCE_PROCESSOR_H
#define KPENGINE_RUNTIME_RESOURCE_PROCESSOR_H

#include <vector>
#include <memory>
#include "base/type.h"
#include "asset/asset.h"

namespace kpengine::resource{
    using AssetPtr = std::shared_ptr<asset::Asset>;

    class IResourceProcessor{
    public:
        virtual void Initialize(GraphicsAPIType api_type) = 0;
        virtual void PreProcess(const std::vector<AssetPtr>& assets){};
        virtual void Process(const std::vector<AssetPtr>& assets) = 0;
        virtual ~IResourceProcessor() = default;
    };
    
}

#endif