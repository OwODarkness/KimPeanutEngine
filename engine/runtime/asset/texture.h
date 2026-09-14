#ifndef KPENGINE_RUNTIME_ASSET_TEXTURE_RESOURCE_H
#define KPENGINE_RUNTIME_ASSET_TEXTURE_RESOURCE_H


#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include "asset_payload.h"
#include "data/texture.h"


namespace kpengine::asset{
    using TextureData = kpengine::data::TextureData;

    struct TextureResource final : IAssetPayload{
        std::shared_ptr<TextureData> data;
        uint32_t channel_count;
        // The initial view is immutable after Asset publication. The loader
        // starts only after Render has committed the initial scene view.
        void SetFullResolutionLoader(
            std::function<std::shared_ptr<const TextureData>()> loader)
        {
            std::lock_guard<std::mutex> lock(residency_mutex_);
            full_resolution_loader_ = std::move(loader);
        }

        void StartFullResolutionLoad() const
        {
            std::lock_guard<std::mutex> lock(residency_mutex_);
            if (!full_resolution_data_.valid() && full_resolution_loader_)
            {
                full_resolution_data_ = std::async(
                    std::launch::async, std::move(full_resolution_loader_)).share();
            }
        }

        std::shared_ptr<const TextureData> TryGetFullResolutionData() const
        {
            std::shared_future<std::shared_ptr<const TextureData>> result;
            {
                std::lock_guard<std::mutex> lock(residency_mutex_);
                result = full_resolution_data_;
            }
            if (!result.valid() ||
                result.wait_for(std::chrono::milliseconds{0}) !=
                    std::future_status::ready)
            {
                return {};
            }
            try
            {
                return result.get();
            }
            catch (...)
            {
                return {};
            }
        }

        TextureResource():data(std::make_shared<TextureData>()){}

    private:
        mutable std::mutex residency_mutex_;
        mutable std::shared_future<std::shared_ptr<const TextureData>>
            full_resolution_data_;
        mutable std::function<std::shared_ptr<const TextureData>()>
            full_resolution_loader_;

        AssetType GetAssetType() const noexcept override
        {
            return AssetType::KPAT_Texture;
        }
    };
}

#endif
