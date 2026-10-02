#ifndef KPENGINE_RUNTIME_ASSET_NATIVE_AUDIO_LOADER_H
#define KPENGINE_RUNTIME_ASSET_NATIVE_AUDIO_LOADER_H

#include <string>

#include "asset.h"

namespace kpengine::asset
{
    class NativeAudioLoader final
    {
    public:
        bool Load(const std::string &path, AssetRegisterInfo &info) const;
    };
}

#endif
