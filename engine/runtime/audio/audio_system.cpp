#include "audio_system.h"
#include "log/logger.h"
#include "audio_player.h"
#include "buffer_audio_player.h"
#include "stream_audio_player.h"
#include <algorithm>

namespace kpengine::audio
{
    AudioSystem::AudioSystem()
        : player_snapshot_(std::make_shared<const PlayerList>())
    {
    }
    AudioSystem::~AudioSystem() = default;
    
    std::shared_ptr<AudioPlayer> AudioSystem::GetAudioPlayer(AudioHandle handle)
    {
        std::lock_guard lock(players_mutex_);
        uint32_t index = handle_system_.Get(handle);
        if (index >= players_.size())
        {
            const char *msg = "Failed to get audio by handle";
            KP_LOG("AudioSystemLog", LOG_LEVEL_ERROR, msg);
            return nullptr;
        }
        return players_[index];
    }

    AudioHandle AudioSystem::CreateAudioPlayer(AudioPlayerType type)
    {
        std::lock_guard lock(players_mutex_);
        ReclaimRetiredPlayers();
        constexpr size_t kMaxActiveVoices = 64;
        if (player_snapshot_.load(std::memory_order_acquire)->size() >= kMaxActiveVoices)
            return {};
        AudioHandle handle = handle_system_.Create();
        std::shared_ptr<AudioPlayer> player;
        if(type == AudioPlayerType::Buffer)
        {
            player = std::make_shared<BufferAudioPlayer>();
        }
        else
        {
            player = std::make_shared<StreamAudioPlayer>();
        }
        if (handle.id == players_.size())
        {
            players_.push_back(std::move(player));
            active_handles_.push_back(handle);
        }
        else
        {
            players_[handle.id] = std::move(player);
            active_handles_[handle.id] = handle;
        }
        PublishPlayerSnapshot();
        return handle;
    }

    bool AudioSystem::DestroyAudioPlayer(AudioHandle handle)
    {
        std::lock_guard lock(players_mutex_);
        ReclaimRetiredPlayers();
        const uint32_t index = handle_system_.Get(handle);
        if (index >= players_.size())
        {
            return false;
        }
        auto retired = players_[index];
        retired->Stop();
        if (!handle_system_.Destroy(handle))
        {
            return false;
        }
        players_[index].reset();
        retired_players_.push_back(std::move(retired));
        PublishPlayerSnapshot();
        return true;
    }

    std::shared_ptr<const AudioSystem::PlayerList> AudioSystem::GetPlayerSnapshot() const
    {
        return player_snapshot_.load(std::memory_order_acquire);
    }

    void AudioSystem::PublishPlayerSnapshot()
    {
        auto previous = player_snapshot_.load(std::memory_order_acquire);
        auto snapshot = std::make_shared<PlayerList>();
        snapshot->reserve(players_.size());
        for (uint32_t id = 0; id < players_.size(); ++id)
        {
            if (players_[id] && handle_system_.IsHandleValid(active_handles_[id]))
                snapshot->push_back(players_[id]);
        }
        retired_snapshots_.push_back(std::move(previous));
        player_snapshot_.store(std::move(snapshot), std::memory_order_release);
    }

    void AudioSystem::ReclaimRetiredPlayers()
    {
        retired_snapshots_.erase(
            std::remove_if(retired_snapshots_.begin(), retired_snapshots_.end(),
                           [](const auto& snapshot) { return snapshot.use_count() == 1; }),
            retired_snapshots_.end());
        retired_players_.erase(
            std::remove_if(retired_players_.begin(), retired_players_.end(),
                           [](const auto& player) { return player.use_count() == 1; }),
            retired_players_.end());
    }

} // namespace kpengine::audio
