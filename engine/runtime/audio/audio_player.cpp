#include "audio_player.h"
#include "log/logger.h"
namespace kpengine::audio
{
    static const char *LogName = "AudioPlayerLog";



    void AudioPlayer::Play()
    {
        state_ = AudioState::Playing;
    }

    void AudioPlayer::Stop()
    {
        current_frame_ = 0;
        state_ = AudioState::Stopped;
        KP_LOG(LogName, LOG_LEVEL_INFO, "Audio stop play");
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
        volume_ = volume;
    }



    bool AudioPlayer::SetCurrentFrame(uint64_t new_frame)
    {
        if(!ResolveFrame(new_frame))
        {
            return false;
        }
        current_frame_ = new_frame;
        return true;
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