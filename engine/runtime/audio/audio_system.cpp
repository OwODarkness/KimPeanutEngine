#include "audio_system.h"
#include "log/logger.h"
#include "audio_player.h"

namespace kpengine::audio
{
    AudioSystem::AudioSystem()
    {

    }
    AudioSystem::~AudioSystem() = default;
        AudioPlayer*  AudioSystem::GetAudioPlayer(AudioHandle handle)
    {
        uint32_t index = handle_system_.Get(handle);
        if(index >= players_.size())
        {
            const char* msg = "Failed to get audio by handle";
            KP_LOG("AudioSystem", LOG_LEVEL_ERROR, msg);
            throw std::runtime_error(msg);
        }
        return players_[index].get();
    }

    AudioHandle  AudioSystem::CreateAudioPlayer()
    {
        AudioHandle handle = handle_system_.Create();
        if(handle.id == players_.size())
        {
            players_.emplace_back(std::make_unique<AudioPlayer>());
        }
        return handle;
    }

       bool AudioSystem::DestroyAudioPlayer(AudioHandle handle)
        {
            AudioPlayer* player = GetAudioPlayer(handle);
            if(!player)
            {
                return false;
            }
            player->Reset();
            return true;

        }
    
} // namespace kpengine::audio
