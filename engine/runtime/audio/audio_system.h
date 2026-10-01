#ifndef KPENGINE_RUNTIME_AUDIO_SYSTEM_H
#define KPENGINE_RUNTIME_AUDIO_SYSTEM_H


#include <cstdint>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include "audio_types.h"

namespace kpengine::audio{
    class AudioPlayer;


    class AudioSystem{
    public:
        AudioSystem();
        virtual  ~AudioSystem();
        virtual bool Initialize() = 0;
        virtual void ShutDown() = 0;
        virtual bool IsInitialized() const { return false; }
        virtual void Mix(float* source, uint32_t frame_count) = 0;

        virtual std::shared_ptr<AudioPlayer> GetAudioPlayer(AudioHandle handle);
        virtual AudioHandle CreateAudioPlayer(AudioPlayerType type) ;
        virtual bool DestroyAudioPlayer(AudioHandle handle);
    protected:
        HandleSystem<AudioHandle> handle_system_;
        using PlayerList = std::vector<std::shared_ptr<class AudioPlayer>>;
        std::vector<std::shared_ptr<class AudioPlayer>> players_;
        std::vector<AudioHandle> active_handles_;
        std::vector<std::shared_ptr<class AudioPlayer>> retired_players_;
        std::atomic<std::shared_ptr<const PlayerList>> player_snapshot_;
        std::vector<std::shared_ptr<const PlayerList>> retired_snapshots_;
        mutable std::mutex players_mutex_;

        std::shared_ptr<const PlayerList> GetPlayerSnapshot() const;
        void PublishPlayerSnapshot();
        void ReclaimRetiredPlayers();

    };
}

#endif
