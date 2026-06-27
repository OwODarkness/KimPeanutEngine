#ifndef KPENGINE_RUNTIME_ASSET_MINIAUDIO_AUDIO_LOADER_H
#define KPENGINE_RUNTIME_ASSET_MINIAUDIO_AUDIO_LOADER_H

#include "audio_loader.h"

namespace kpengine::asset{

class MiniAudio_AudioLoader : public IAudioLoader{
public:
        MiniAudio_AudioLoader();
        ~MiniAudio_AudioLoader();
        bool LoadFromFile(const std::string& path, AssetRegisterInfo& info) override; 
        static AudioResource LoadFromMemory(char* src, size_t size) ;
private:
        MiniAudio_AudioLoader(const MiniAudio_AudioLoader&) = delete;
        MiniAudio_AudioLoader& operator=(const MiniAudio_AudioLoader&) = delete;

};

}

#endif