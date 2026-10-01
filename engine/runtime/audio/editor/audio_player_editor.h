#ifndef KPENGINE_RUNTIME_AUDIO_EDITOR_AUDIO_PLAYER_EDITOR_H
#define KPENGINE_RUNTIME_AUDIO_EDITOR_AUDIO_PLAYER_EDITOR_H

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "editor/ui/component/editor_layout_model.h"
#include "editor/ui/component/editor_splitter_handles.h"
#include "editor/ui/component/editor_tool_row_model.h"

namespace kpengine::audio
{
    class MiniAudioSystem;
}

namespace kpengine::editor
{
    class EditorAudioComponent;
    class EditorUI;
    class EditorToolRowComponent;
}

namespace kpengine::audio_player
{
    class AudioPlayerController;
}

namespace kpengine::runtime
{
    class Engine;
}

namespace kpengine::audio_player
{
    class AudioPlayerEditor final
    {
    public:
        AudioPlayerEditor();
        ~AudioPlayerEditor();

        AudioPlayerEditor(const AudioPlayerEditor &) = delete;
        AudioPlayerEditor &operator=(const AudioPlayerEditor &) = delete;

        bool Initialize(runtime::Engine &engine, audio::MiniAudioSystem &audio_system,
                        AudioPlayerController &controller,
                        std::string &diagnostic);
        bool Render(std::string &diagnostic);
        void Shutdown() noexcept;

    private:
        void RenderDockedWorkspace();
        void ApplyLayout();

        std::unique_ptr<editor::EditorUI> ui_;
        std::unique_ptr<editor::EditorAudioComponent> audio_panel_;
        editor::EditorLayoutModel layout_;
        editor::EditorToolRowModel dock_model_;
        editor::EditorSplitterHandles splitter_handles_;
        std::unique_ptr<editor::EditorToolRowComponent> dock_host_;
        AudioPlayerController *controller_ = nullptr;
        audio::MiniAudioSystem *audio_system_ = nullptr;
        char import_path_[1024]{};
        char search_[256]{};
        std::array<std::vector<std::uint8_t>, 7> playback_icon_alpha_{};
        int library_filter_ = 0;
        float playback_rate_ = 1.0f;
        bool show_diagnostics_ = false;
        std::string ui_status_;
        std::string ui_error_;
    };
}

#endif
