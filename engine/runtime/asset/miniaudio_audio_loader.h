#ifndef KPENGINE_RUNTIME_ASSET_MINIAUDIO_AUDIO_LOADER_H
#define KPENGINE_RUNTIME_ASSET_MINIAUDIO_AUDIO_LOADER_H

#include <memory>
#include "audio_loader.h"

namespace kpengine::asset{

class MiniAudio_AudioLoader : public IAudioLoader{
public:
        MiniAudio_AudioLoader();
        ~MiniAudio_AudioLoader() override;
        MiniAudio_AudioLoader(const MiniAudio_AudioLoader&) = delete;
        MiniAudio_AudioLoader& operator=(const MiniAudio_AudioLoader&) = delete;
        bool LoadFromFile(const std::string& path, AssetRegisterInfo& info) override;
        AudioResource LoadFromMemory(const char* src, size_t size) override;
private:
        // Holds the reusable file decoder; defined in the .cpp so miniaudio
        // headers stay out of this public header.
        struct Impl;
        std::unique_ptr<Impl> impl_;
};

}

#endif
