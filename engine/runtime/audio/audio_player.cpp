#include "audio_player.h"

#include <algorithm>

namespace kpengine::audio
{
    AudioPlayer::AudioPlayer()
    {
        for (size_t index = 0; index < command_queue_.size(); ++index)
            command_queue_[index].sequence.store(index, std::memory_order_relaxed);
    }

    bool AudioPlayer::QueueCommand(Command command, uint64_t value)
    {
        size_t position = command_write_position_.load(std::memory_order_relaxed);
        for (;;)
        {
            auto& slot = command_queue_[position & (kCommandQueueCapacity - 1)];
            const size_t sequence = slot.sequence.load(std::memory_order_acquire);
            if (sequence == position)
            {
                if (command_write_position_.compare_exchange_weak(
                        position, position + 1, std::memory_order_relaxed))
                {
                    slot.command = command;
                    slot.value = value;
                    slot.sequence.store(position + 1, std::memory_order_release);
                    return true;
                }
                continue;
            }
            if (sequence < position)
            {
                control_command_rejections_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            position = command_write_position_.load(std::memory_order_relaxed);
        }
    }

    bool AudioPlayer::DequeueCommand(Command& command, uint64_t& value)
    {
        auto& slot = command_queue_[command_read_position_ & (kCommandQueueCapacity - 1)];
        const size_t sequence = slot.sequence.load(std::memory_order_acquire);
        if (sequence != command_read_position_ + 1)
            return false;
        command = slot.command;
        value = slot.value;
        slot.sequence.store(command_read_position_ + kCommandQueueCapacity,
                            std::memory_order_release);
        ++command_read_position_;
        return true;
    }

    void AudioPlayer::Play()
    {
        if (CanStartPlayback())
            QueueCommand(Command::Play);
    }

    void AudioPlayer::Stop()
    {
        QueueCommand(Command::Stop);
    }

    void AudioPlayer::Pause()
    {
        QueueCommand(Command::Pause);
    }

    void AudioPlayer::Reset()
    {
        QueueCommand(Command::Reset);
    }

    void AudioPlayer::Restart()
    {
        if (CanStartPlayback())
            QueueCommand(Command::Restart);
    }

    void AudioPlayer::SetVolume(float volume)
    {
        volume_.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_release);
    }

    void AudioPlayer::SetShouldLoop(bool looping)
    {
        QueueCommand(Command::SetLoop, looping ? 1u : 0u);
    }

    bool AudioPlayer::IsPlaying() const
    {
        const auto state = state_.load(std::memory_order_acquire);
        return state == AudioState::Playing || state == AudioState::Buffering;
    }

    bool AudioPlayer::IsActive() const
    {
        const auto state = state_.load(std::memory_order_acquire);
        return state == AudioState::Playing || state == AudioState::Buffering ||
               state == AudioState::FadingOut;
    }

    bool AudioPlayer::CopyFrameData(uint64_t frame, float* out_data,
                                    uint32_t capacity_samples, uint32_t& channels)
    {
        channels = GetAudioFormat().channels;
        if (channels == 0 || capacity_samples < channels)
            return false;
        uint32_t copied_channels = 0;
        const bool copied = CopyFrames(frame, out_data, 1, copied_channels) == 1;
        channels = copied_channels;
        return copied;
    }

    bool AudioPlayer::SetCurrentFrame(uint64_t new_frame)
    {
        const bool ready = ResolveFrame(new_frame);
        if (ready)
            current_frame_.store(new_frame, std::memory_order_release);
        return ready;
    }

    bool AudioPlayer::AdvanceFrame()
    {
        CommitPlayedFrames(1);
        return IsActive();
    }

    bool AudioPlayer::AdvanceSecond()
    {
        return SeekSeconds(GetCurrentSecond() + 1);
    }

    bool AudioPlayer::SeekFrames(uint64_t new_frame)
    {
        return QueueCommand(Command::Seek, new_frame);
    }

    void AudioPlayer::ApplyPendingCommand(uint32_t gain_ramp_frames)
    {
        gain_ramp_duration_frames_ = gain_ramp_frames;
        const size_t write_snapshot = command_write_position_.load(std::memory_order_acquire);
        const size_t queued = std::min(write_snapshot - command_read_position_,
                                       kCommandQueueCapacity);
        for (size_t index = 0; index < queued; ++index)
        {
            Command command{};
            uint64_t value = 0;
            if (!DequeueCommand(command, value))
                break;
            ApplyCommand(command, value);
        }
    }

    void AudioPlayer::ApplyCommand(Command command, uint64_t value)
    {
        switch (command)
        {
        case Command::Play:
            if (CanStartPlayback())
            {
                if (!IsActive())
                    voice_gain_current_ = 0.0f;
                state_.store(AudioState::Playing, std::memory_order_release);
                BeginGainRamp(volume_.load(std::memory_order_acquire));
            }
            break;
        case Command::Pause:
            if (IsActive())
            {
                fade_completion_state_ = AudioState::Paused;
                state_.store(AudioState::FadingOut, std::memory_order_release);
                BeginGainRamp(0.0f);
            }
            else
            {
                state_.store(AudioState::Paused, std::memory_order_release);
            }
            break;
        case Command::Stop:
            if (IsActive())
            {
                fade_completion_state_ = AudioState::Stopped;
                state_.store(AudioState::FadingOut, std::memory_order_release);
                BeginGainRamp(0.0f);
            }
            else
            {
                current_frame_.store(0, std::memory_order_release);
                state_.store(AudioState::Stopped, std::memory_order_release);
                BeginGainRamp(0.0f);
            }
            break;
        case Command::Reset:
            current_frame_.store(0, std::memory_order_release);
            state_.store(AudioState::Stopped, std::memory_order_release);
            BeginGainRamp(0.0f);
            break;
        case Command::Restart:
            if (CanStartPlayback())
            {
                current_frame_.store(0, std::memory_order_release);
                voice_gain_current_ = 0.0f;
                state_.store(AudioState::Playing, std::memory_order_release);
                BeginGainRamp(volume_.load(std::memory_order_acquire));
            }
            break;
        case Command::Seek:
            SetCurrentFrame(value);
            break;
        case Command::SetLoop:
            looping_.store(value != 0, std::memory_order_release);
            break;
        }
    }

    void AudioPlayer::BeginMixCallback(uint32_t gain_ramp_frames)
    {
        gain_ramp_duration_frames_ = gain_ramp_frames;
        const auto state = state_.load(std::memory_order_relaxed);
        if (state == AudioState::Playing || state == AudioState::Buffering)
            BeginGainRamp(volume_.load(std::memory_order_relaxed));
    }

    void AudioPlayer::CommitPlayedFrames(uint32_t frame_count)
    {
        if (state_.load(std::memory_order_acquire) == AudioState::Stopped &&
            fade_completion_state_ == AudioState::Stopped)
        {
            current_frame_.store(0, std::memory_order_release);
            return;
        }
        if (frame_count == 0)
            return;
        OnPlaybackCursorAdvanced(frame_count);
        if (IsSourceDrained())
            MarkFinished();
    }

    void AudioPlayer::MarkBuffering()
    {
        AudioState expected = AudioState::Playing;
        state_.compare_exchange_strong(expected, AudioState::Buffering,
                                       std::memory_order_acq_rel);
    }

    void AudioPlayer::MarkPlaying()
    {
        AudioState expected = AudioState::Buffering;
        state_.compare_exchange_strong(expected, AudioState::Playing,
                                       std::memory_order_acq_rel);
    }

    void AudioPlayer::RefreshSourceState()
    {
        if (IsSourceDrained())
            MarkFinished();
    }

    float AudioPlayer::NextVoiceGain()
    {
        if (voice_gain_ramp_remaining_ > 0)
        {
            voice_gain_current_ +=
                (voice_gain_target_ - voice_gain_current_) /
                static_cast<float>(voice_gain_ramp_remaining_);
            --voice_gain_ramp_remaining_;
            if (voice_gain_ramp_remaining_ == 0)
            {
                voice_gain_current_ = voice_gain_target_;
                FinishFadeIfNeeded();
            }
        }
        return voice_gain_current_;
    }

    void AudioPlayer::BeginGainRamp(float target)
    {
        if (target != voice_gain_target_)
        {
            voice_gain_target_ = target;
            voice_gain_ramp_remaining_ = gain_ramp_duration_frames_;
            if (voice_gain_ramp_remaining_ == 0)
                voice_gain_current_ = target;
        }
        else if (voice_gain_ramp_remaining_ == 0 && voice_gain_current_ != target)
        {
            voice_gain_ramp_remaining_ = gain_ramp_duration_frames_;
            if (voice_gain_ramp_remaining_ == 0)
                voice_gain_current_ = target;
        }
    }

    void AudioPlayer::FinishFadeIfNeeded()
    {
        if (state_.load(std::memory_order_relaxed) != AudioState::FadingOut)
            return;
        state_.store(fade_completion_state_, std::memory_order_release);
    }

    void AudioPlayer::MarkFinished()
    {
        state_.store(AudioState::Finished, std::memory_order_release);
        voice_gain_target_ = 0.0f;
        voice_gain_current_ = 0.0f;
        voice_gain_ramp_remaining_ = 0;
    }

    void AudioPlayer::MarkCancelled()
    {
        state_.store(AudioState::Cancelled, std::memory_order_release);
        voice_gain_target_ = 0.0f;
        voice_gain_current_ = 0.0f;
        voice_gain_ramp_remaining_ = 0;
    }
}
