#ifndef KPENGINE_EDITOR_AUDIO_COMPONENT_H
#define KPENGINE_EDITOR_AUDIO_COMPONENT_H

#include <chrono>

#include "editor/ui/component/editor_window_component.h"

namespace kpengine::audio
{
    class MiniAudioSystem;
}

namespace kpengine::editor
{
    class EditorAudioComponent final : public EditorWindowComponent
    {
    public:
        explicit EditorAudioComponent(audio::MiniAudioSystem *audio_system);
        void RenderContent() override;

    private:
        audio::MiniAudioSystem *audio_system_ = nullptr;
        float master_gain_ = 1.0f;
        float speech_gain_ = 1.0f;
        float music_gain_ = 1.0f;
        bool speech_muted_ = false;
        bool music_muted_ = false;
        bool initialization_failed_ = false;
        std::chrono::steady_clock::time_point last_activity_sample_{};
        std::uint64_t last_speech_frames_ = 0;
        std::uint64_t last_music_frames_ = 0;
        float speech_activity_ = 0.0f;
        float music_activity_ = 0.0f;
    };
}

#endif
