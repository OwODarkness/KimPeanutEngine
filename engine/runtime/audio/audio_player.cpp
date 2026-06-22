#include "audio_player.h"
#include "audio_clip.h"

namespace kpengine::audio
{

    void  AudioPlayer::SetClip(std::shared_ptr<AudioClip> clip)
    {
        clip_ = clip;
    }

    void AudioPlayer::Play()
    {
        playing = true;
    }

    void AudioPlayer::Stop()
    {
        current_frame_ = 0;
        playing = false;
    }

    void AudioPlayer::Pause()
    {
        playing = false;
    }


} // namespace kpengine::audio
