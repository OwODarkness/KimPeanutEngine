#include "audio_system.h"

#include "audio_player.h"
#include "buffer_audio_player.h"
#include "stream_audio_player.h"
#include "seekable_audio_player.h"
#include "audio_stream.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace kpengine::audio
{
    void AudioSystem::GainRamp::Retarget(float new_target)
    {
        if (new_target == target)
            return;
        target = new_target;
        remaining = duration_frames;
        if (remaining == 0)
            current = target;
    }

    float AudioSystem::GainRamp::Next()
    {
        if (remaining > 0)
        {
            current += (target - current) / static_cast<float>(remaining);
            --remaining;
            if (remaining == 0)
                current = target;
        }
        return current;
    }

    AudioSystem::AudioSystem(AudioSystemSettings settings)
        : settings_(settings)
    {
        settings_.max_voices = std::clamp(settings_.max_voices, 1u, kMaxVoices);
        settings_.max_callback_frames =
            std::clamp(settings_.max_callback_frames, 1u, kMaxCallbackFrames);
        settings_.device_period_count = std::clamp(settings_.device_period_count, 1u, 8u);
        master_gain_ramp_.duration_frames = settings_.gain_ramp_frames;
        for (auto& ramp : bus_gain_ramps_)
            ramp.duration_frames = settings_.gain_ramp_frames;
        for (auto& gain : bus_gain_target_)
            gain.store(1.0f, std::memory_order_relaxed);
        for (auto& muted : bus_muted_)
            muted.store(false, std::memory_order_relaxed);
    }

    AudioSystem::~AudioSystem()
    {
        for (auto& voice : voices_)
            voice.published.store(nullptr, std::memory_order_seq_cst);
        WaitForCallbacks();
    }

    std::shared_ptr<AudioPlayer> AudioSystem::GetAudioPlayer(AudioHandle handle)
    {
        std::lock_guard lock(voices_mutex_);
        const uint32_t index = handle_system_.Get(handle);
        if (index >= voices_.size())
            return nullptr;
        const auto& voice = voices_[index];
        return voice.handle == handle ? voice.owner : nullptr;
    }

    AudioHandle AudioSystem::CreateAudioPlayer(AudioPlayerType type)
    {
        std::lock_guard lock(voices_mutex_);
        AudioHandle handle = handle_system_.Create();
        if (!handle.IsValid() || handle.id >= settings_.max_voices)
        {
            if (handle.IsValid())
                handle_system_.Destroy(handle);
            return {};
        }

        auto& voice = voices_[handle.id];
        if (voice.owner)
        {
            handle_system_.Destroy(handle);
            return {};
        }

        switch (type)
        {
        case AudioPlayerType::Buffer:
            voice.owner = std::make_shared<BufferAudioPlayer>();
            break;
        case AudioPlayerType::Stream:
            voice.owner = std::make_shared<StreamAudioPlayer>();
            break;
        case AudioPlayerType::Seekable:
            voice.owner = std::make_shared<SeekableAudioPlayer>();
            break;
        default:
            handle_system_.Destroy(handle);
            return {};
        }
        voice.handle = handle;
        voice.published.store(voice.owner.get(), std::memory_order_seq_cst);
        return handle;
    }

    bool AudioSystem::DestroyAudioPlayer(AudioHandle handle)
    {
        std::shared_ptr<AudioPlayer> retired;
        VoiceSlot* voice = nullptr;
        {
            std::lock_guard lock(voices_mutex_);
            const uint32_t index = handle_system_.Get(handle);
            if (index >= voices_.size())
                return false;
            voice = &voices_[index];
            if (voice->handle != handle || !voice->owner)
                return false;

            if (const auto* stream_player = dynamic_cast<const StreamAudioPlayer*>(voice->owner.get()))
            {
                if (const auto stream = stream_player->GetStream())
                    retired_stream_overflow_rejections_.fetch_add(
                        stream->GetOverflowRejectionCount(), std::memory_order_relaxed);
            }
            retired_control_command_rejections_.fetch_add(
                voice->owner->GetControlCommandRejectionCount(), std::memory_order_relaxed);
            voice->published.exchange(nullptr, std::memory_order_seq_cst);
            if (!handle_system_.Destroy(handle))
                return false;
            retired = std::move(voice->owner);
            voice->handle = {};
        }

        while (voice->callback_readers.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        retired.reset();
        return true;
    }

    void AudioSystem::SetMasterGain(float gain)
    {
        master_gain_target_.store(std::clamp(gain, 0.0f, 1.0f), std::memory_order_release);
    }

    float AudioSystem::GetMasterGain() const
    {
        return master_gain_target_.load(std::memory_order_acquire);
    }

    size_t AudioSystem::BusIndex(AudioBus bus)
    {
        const size_t index = static_cast<size_t>(bus);
        return index < kBusCount ? index : 0;
    }

    void AudioSystem::SetBusGain(AudioBus bus, float gain)
    {
        bus_gain_target_[BusIndex(bus)].store(std::clamp(gain, 0.0f, 1.0f),
                                              std::memory_order_release);
    }

    float AudioSystem::GetBusGain(AudioBus bus) const
    {
        return bus_gain_target_[BusIndex(bus)].load(std::memory_order_acquire);
    }

    void AudioSystem::SetBusMuted(AudioBus bus, bool muted)
    {
        bus_muted_[BusIndex(bus)].store(muted, std::memory_order_release);
    }

    bool AudioSystem::IsBusMuted(AudioBus bus) const
    {
        return bus_muted_[BusIndex(bus)].load(std::memory_order_acquire);
    }

    AudioTelemetrySnapshot AudioSystem::GetTelemetrySnapshot() const
    {
        AudioTelemetrySnapshot snapshot{};
        snapshot.callback_count = callback_count_.load(std::memory_order_acquire);
        snapshot.callback_frames = callback_frames_.load(std::memory_order_acquire);
        snapshot.callback_work_limited_frames =
            callback_work_limited_frames_.load(std::memory_order_acquire);
        snapshot.max_callback_duration_ns = max_callback_duration_ns_.load(std::memory_order_acquire);
        snapshot.concurrent_callback_rejections =
            concurrent_callback_rejections_.load(std::memory_order_acquire);
        snapshot.stream_overflow_rejections =
            retired_stream_overflow_rejections_.load(std::memory_order_acquire);
        snapshot.control_command_rejections =
            retired_control_command_rejections_.load(std::memory_order_acquire);
        for (size_t index = 0; index < kBusCount; ++index)
        {
            snapshot.buses[index].played_frames =
                bus_played_frames_[index].load(std::memory_order_acquire);
            snapshot.buses[index].underrun_blocks =
                bus_underrun_blocks_[index].load(std::memory_order_acquire);
        }

        std::lock_guard lock(voices_mutex_);
        for (const auto& voice : voices_)
        {
            if (voice.owner)
                snapshot.control_command_rejections +=
                    voice.owner->GetControlCommandRejectionCount();
            const auto* stream_player = dynamic_cast<const StreamAudioPlayer*>(voice.owner.get());
            if (stream_player)
            {
                const auto stream = stream_player->GetStream();
                if (stream)
                    snapshot.stream_overflow_rejections += stream->GetOverflowRejectionCount();
            }
        }
        return snapshot;
    }

    void AudioSystem::Mix(float* output, uint32_t frame_count)
    {
        constexpr uint32_t output_channels = 2;
        if (!output || frame_count == 0)
            return;

        if (mix_guard_.test_and_set(std::memory_order_acquire))
        {
            std::memset(output, 0, static_cast<size_t>(frame_count) * output_channels * sizeof(float));
            concurrent_callback_rejections_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        struct MixGuard
        {
            std::atomic_flag& flag;
            ~MixGuard() { flag.clear(std::memory_order_release); }
        } mix_guard{mix_guard_};

        const auto started = std::chrono::steady_clock::now();
        std::memset(output, 0, static_cast<size_t>(frame_count) * output_channels * sizeof(float));
        callback_count_.fetch_add(1, std::memory_order_relaxed);
        callback_frames_.fetch_add(frame_count, std::memory_order_relaxed);
        const uint32_t work_frame_count = std::min(frame_count, settings_.max_callback_frames);
        if (frame_count > work_frame_count)
            callback_work_limited_frames_.fetch_add(frame_count - work_frame_count,
                                                    std::memory_order_relaxed);

        std::array<AudioPlayer*, kMaxVoices> players{};
        std::array<size_t, kMaxVoices> player_buses{};
        for (size_t index = 0; index < settings_.max_voices; ++index)
        {
            auto& voice = voices_[index];
            voice.callback_readers.fetch_add(1, std::memory_order_seq_cst);
            players[index] = voice.published.load(std::memory_order_seq_cst);
            if (players[index])
            {
                players[index]->ApplyPendingCommand(settings_.gain_ramp_frames);
                players[index]->BeginMixCallback(settings_.gain_ramp_frames);
                player_buses[index] = BusIndex(players[index]->GetBus());
            }
            else
                voice.callback_readers.fetch_sub(1, std::memory_order_seq_cst);
        }

        const float master_target = master_gain_target_.load(std::memory_order_relaxed);
        master_gain_ramp_.Retarget(master_target);
        for (size_t bus = 0; bus < kBusCount; ++bus)
        {
            const float gain = bus_muted_[bus].load(std::memory_order_relaxed)
                                   ? 0.0f
                                   : bus_gain_target_[bus].load(std::memory_order_relaxed);
            bus_gain_ramps_[bus].Retarget(gain);
        }

        for (uint32_t offset = 0; offset < work_frame_count;)
        {
            const uint32_t block_frames =
                std::min(settings_.max_callback_frames, work_frame_count - offset);
            for (uint32_t frame = 0; frame < block_frames; ++frame)
            {
                master_gain_scratch_[frame] = master_gain_ramp_.Next();
                for (size_t bus = 0; bus < kBusCount; ++bus)
                    bus_gain_scratch_[bus][frame] = bus_gain_ramps_[bus].Next();
            }

            for (size_t voice_index = 0; voice_index < settings_.max_voices; ++voice_index)
            {
                AudioPlayer* player = players[voice_index];
                if (!player || !player->IsActive())
                    continue;

                player->FillBuffer();
                uint32_t source_channels = 0;
                const float playback_rate = player->GetPlaybackRate();
                const uint32_t copied = player->CopyFramesAtRate(
                    player->GetCurrentFrame(), voice_scratch_.data(), block_frames,
                    source_channels, playback_rate);
                const size_t bus = player_buses[voice_index];
                uint32_t consumed = 0;
                for (; consumed < copied; ++consumed)
                {
                    const float gain = player->NextVoiceGain() * master_gain_scratch_[consumed] *
                                       bus_gain_scratch_[bus][consumed];
                    const size_t source_index = static_cast<size_t>(consumed) * source_channels;
                    const size_t destination_index =
                        static_cast<size_t>(offset + consumed) * output_channels;
                    if (source_channels == 1)
                    {
                        output[destination_index] += voice_scratch_[source_index] * gain;
                        output[destination_index + 1] += voice_scratch_[source_index] * gain;
                    }
                    else if (source_channels >= 2)
                    {
                        output[destination_index] += voice_scratch_[source_index] * gain;
                        output[destination_index + 1] += voice_scratch_[source_index + 1] * gain;
                    }

                    if (!player->IsActive())
                    {
                        ++consumed;
                        break;
                    }
                }
                player->CommitPlayedFrames(consumed, playback_rate);
                player->RefreshSourceState();

                if (consumed > 0)
                    bus_played_frames_[bus].fetch_add(consumed, std::memory_order_relaxed);
                if (player->IsStreamingSourcePublic() && copied < block_frames &&
                    !player->IsFinished() && !player->IsCancelled())
                {
                    if (copied == 0 && player->GetCurrentState() == AudioState::Playing)
                        player->MarkBuffering();
                    bus_underrun_blocks_[bus].fetch_add(1, std::memory_order_relaxed);
                }
            }
            offset += block_frames;
        }

        for (size_t index = 0; index < voices_.size(); ++index)
        {
            if (players[index])
                voices_[index].callback_readers.fetch_sub(1, std::memory_order_seq_cst);
        }

        for (size_t sample = 0;
             sample < static_cast<size_t>(work_frame_count) * output_channels; ++sample)
            output[sample] = std::clamp(output[sample], -1.0f, 1.0f);

        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        const uint64_t duration = elapsed > 0 ? static_cast<uint64_t>(elapsed) : 0;
        uint64_t max_duration = max_callback_duration_ns_.load(std::memory_order_relaxed);
        if (duration > max_duration)
            max_callback_duration_ns_.compare_exchange_strong(
                max_duration, duration, std::memory_order_relaxed);
    }

    void AudioSystem::WaitForCallbacks()
    {
        for (auto& voice : voices_)
        {
            while (voice.callback_readers.load(std::memory_order_acquire) != 0)
                std::this_thread::yield();
        }
    }
}
