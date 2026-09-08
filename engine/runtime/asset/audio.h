#ifndef KPENGINE_RUNTIME_ASSET_AUDIO_H
#define KPENGINE_RUNTIME_ASSET_AUDIO_H

#include <memory>
#include "asset_payload.h"
#include "data/audio.h"

namespace kpengine::asset{
    using AudioClip = kpengine::data::AudioClip;

    struct AudioResource final : IAssetPayload{
        std::shared_ptr<AudioClip> data;

        AssetType GetAssetType() const noexcept override
        {
            return AssetType::KPAT_Audio;
        }
    };
}

#endif
