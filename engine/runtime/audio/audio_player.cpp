#include "audio_player.h"
#include <algorithm>
namespace kpengine::audio
{
    void AudioPlayer::Play()
    {
        state_ = AudioState::Playing;
    }

    void AudioPlayer::Stop()
    {
        current_frame_ = 0;
        state_ = AudioState::Stopped;
    }

    void AudioPlayer::Pause()
    {
        state_ = AudioState::Paused;
    }

    void AudioPlayer::Reset()
    {
        current_frame_ = 0;
        state_ = AudioState::Stopped;
    }

    void AudioPlayer::Restart()
    {
        current_frame_ = 0;
        state_ = AudioState::Playing;
    }

    void AudioPlayer::SetVolume(float volume)
    {
        volume_.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
    }



    bool AudioPlayer::SetCurrentFrame(uint64_t new_frame)
    {
        bool ready = ResolveFrame(new_frame);

        // Always sync the playhead with the buffer window. For a streaming
        // player, ResolveFrame moves the window even when the requested frame
        // is not available yet (it is waiting on a dry source); the playhead
        // must follow, otherwise it would rewind and repeat the last frame.
        current_frame_ = new_frame;
        return ready;
    }



    void AudioPlayer::SetShouldLoop(bool looping)
    {
        looping_ = looping;
    }

    bool AudioPlayer::AdvanceFrame()
    {
        return SeekFrames(current_frame_ + 1);
    }

    bool AudioPlayer::AdvanceSecond()
    {
        return SeekSeconds(GetCurrentSecond() + 1);
    }

    bool AudioPlayer::SeekFrames(uint64_t new_frame)
    {
        return SetCurrentFrame(new_frame);
    }


}
