#ifndef KPENGINE_RUNTIME_ASSET_AUDIO_H
#define KPENGINE_RUNTIME_ASSET_AUDIO_H

#include <memory>
#include "native_audio.h"
#include "asset_payload.h"
#include "data/audio.h"

namespace kpengine::asset{
    using AudioClip = kpengine::data::AudioClip;

    struct AudioResource final : IAssetPayload{
        std::shared_ptr<AudioClip> data;
        // Native music remains file-backed; Audio consumes its range through
        // the seekable source API and retains this object as a lifetime pin.
        std::shared_ptr<const NativeAudioFileProduct> native_product;

        AssetType GetAssetType() const noexcept override
        {
            return AssetType::KPAT_Audio;
        }
    };
}

#endif
