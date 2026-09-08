#ifndef KPENGINE_LIVE2D_MODEL_RESOURCE_H
#define KPENGINE_LIVE2D_MODEL_RESOURCE_H

#include <cstdint>
#include <memory>
#include <utility>
#include "asset/asset_payload.h"
#include "live2d_product.h"

namespace kpengine::live2d
{
    // Custom Asset values are allocated by the Asset extension range. This
    // value is stable because AssetID serializes the type in its high bits.
    inline constexpr std::uint16_t kLive2DModelAssetTypeValue = 0x1000u;
    inline constexpr asset::AssetType kLive2DModelAssetType =
        static_cast<asset::AssetType>(kLive2DModelAssetTypeValue);

    // Immutable data shared by all instances of one imported model. Cubism
    // model objects, parameter values, and frame-local drawable data belong to
    // Live2DSystem and are deliberately absent here.
    class Live2DModelResource final : public asset::IAssetPayload
    {
    public:
        explicit Live2DModelResource(std::shared_ptr<const Live2DProductData> product)
            : product_(std::move(product))
        {
        }

        const Live2DProductData &Product() const noexcept { return *product_; }

        asset::AssetType GetAssetType() const noexcept override
        {
            return kLive2DModelAssetType;
        }

    private:
        std::shared_ptr<const Live2DProductData> product_;
    };
}

#endif
