#ifndef KPENGINE_RUNTIME_ASSET_TEXTURE_RESOURCE_H
#define KPENGINE_RUNTIME_ASSET_TEXTURE_RESOURCE_H


#include <chrono>
#include <future>
#include <memory>
#include "asset_payload.h"
#include "data/texture.h"


namespace kpengine::asset{
    using TextureData = kpengine::data::TextureData;

    struct TextureResource final : IAssetPayload{
        std::shared_ptr<TextureData> data;
        uint32_t channel_count;
        // The initial view is immutable after Asset publication. The future
        // owns the optional full-resolution replacement without coupling
        // Asset to Graphics or Render.
        std::shared_future<std::shared_ptr<const TextureData>> full_resolution_data;

        std::shared_ptr<const TextureData> TryGetFullResolutionData() const
        {
            if (!full_resolution_data.valid() ||
                full_resolution_data.wait_for(std::chrono::milliseconds{0}) !=
                    std::future_status::ready)
            {
                return {};
            }
            try
            {
                return full_resolution_data.get();
            }
            catch (...)
            {
                return {};
            }
        }

        TextureResource():data(std::make_shared<TextureData>()){}

        AssetType GetAssetType() const noexcept override
        {
            return AssetType::KPAT_Texture;
        }
    };
}

#endif
