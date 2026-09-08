#ifndef KPENGINE_RUNTIME_ASSET_ASSET_PAYLOAD_H
#define KPENGINE_RUNTIME_ASSET_ASSET_PAYLOAD_H

#include <memory>

#include "common.h"

namespace kpengine::asset
{
    class IAssetPayload
    {
    public:
        virtual ~IAssetPayload() = default;
        virtual AssetType GetAssetType() const noexcept = 0;
    };

    using AssetPayload = std::shared_ptr<IAssetPayload>;
}

#endif
