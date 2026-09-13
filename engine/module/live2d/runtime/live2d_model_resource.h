#ifndef KPENGINE_LIVE2D_MODEL_RESOURCE_H
#define KPENGINE_LIVE2D_MODEL_RESOURCE_H

#include <cstdint>
#include <memory>
#include <string_view>
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

    struct Live2DBehaviorCapabilities final
    {
        bool has_typed_playback = false;
        bool has_secondary_behavior = false;
        bool has_physics = false;
        bool has_pose = false;
        bool has_hit_areas = false;
        bool has_user_data = false;
        bool requires_reimport_for_secondary_behavior = false;
    };
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
        Live2DBehaviorCapabilities Capabilities() const noexcept
        {
            const Live2DProductData &product = Product();
            Live2DBehaviorCapabilities capabilities{};
            capabilities.has_typed_playback = product.product_version >= 2u;
            capabilities.has_secondary_behavior = product.product_version >= 3u;
            capabilities.has_physics = capabilities.has_secondary_behavior &&
                                       !product.secondary_behavior.physics_bytes.empty();
            capabilities.has_pose = capabilities.has_secondary_behavior &&
                                    !product.secondary_behavior.pose_bytes.empty();
            capabilities.has_hit_areas = capabilities.has_secondary_behavior &&
                                         !product.secondary_behavior.hit_areas.empty();
            capabilities.has_user_data = capabilities.has_secondary_behavior &&
                                         !product.secondary_behavior.user_data.empty();
            capabilities.requires_reimport_for_secondary_behavior =
                !capabilities.has_secondary_behavior;
            return capabilities;
        }
        // Product V3 metadata is immutable and shared by every instance.
        const std::vector<Live2DUserDataEntry> &UserData() const noexcept
        {
            return product_->secondary_behavior.user_data;
        }

        const std::vector<Live2DHitAreaDefinition> &HitAreas() const noexcept
        {
            return product_->secondary_behavior.hit_areas;
        }

        const Live2DUserDataEntry *FindUserData(
            const std::string_view target_type,
            const std::string_view target_id) const noexcept
        {
            for (const Live2DUserDataEntry &entry : UserData())
            {
                if (entry.target_type == target_type && entry.target_id == target_id)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        asset::AssetType GetAssetType() const noexcept override
        {
            return kLive2DModelAssetType;
        }

    private:
        std::shared_ptr<const Live2DProductData> product_;
    };
}

#endif
