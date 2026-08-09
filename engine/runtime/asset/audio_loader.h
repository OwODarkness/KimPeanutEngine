#ifndef KPENGINE_RUNTIME_ASSET_AUDIO_LOADER_H
#define KPENGINE_RUNTIME_ASSET_AUDIO_LOADER_H

#include <string>
#include "asset.h"
#include "audio.h"

namespace kpengine::asset{
    class IAudioLoader{
    public:
        virtual ~IAudioLoader() = default;
        virtual bool LoadFromFile(const std::string& path, AssetRegisterInfo& info) = 0; 
        virtual AudioResource LoadFromMemory(const char* src, size_t size) = 0;
         
    };
}

#endif