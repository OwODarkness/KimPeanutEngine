#ifndef KPENGINE_MODULE_AUDIO_AUDIO_PLAYER_EDITOR_H
#define KPENGINE_MODULE_AUDIO_AUDIO_PLAYER_EDITOR_H

#include <cstdint>
#include <memory>
#include <string>

#include "editor/ui/editor_icons.h"

namespace kpengine::audio
{
    class MiniAudioSystem;
}

namespace kpengine::editor
{
    class EditorUiHost;
    class EditorWorkspace;
    namespace ui { class UiContext; }
}

namespace kpengine { class LogSystem; }

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
        void RenderWorkspace(editor::ui::UiContext &ui);
        void RenderLibrary(editor::ui::UiContext &ui);
        void RenderPlaylists(editor::ui::UiContext &ui);
        void RenderNowPlaying(editor::ui::UiContext &ui);
        void RenderQueue(editor::ui::UiContext &ui);
        void RenderInfo(editor::ui::UiContext &ui);
        void RenderSpectrum(editor::ui::UiContext &ui);
        void RenderOutputLog(editor::ui::UiContext &ui);
        void ReportImport(std::string path, std::string subtitle = {});

        std::unique_ptr<editor::EditorUiHost> ui_;
        std::unique_ptr<editor::EditorWorkspace> workspace_;
        editor::EditorBuiltinIcons icons_;
        AudioPlayerController *controller_ = nullptr;
        audio::MiniAudioSystem *audio_system_ = nullptr;
        LogSystem *log_system_ = nullptr;
        std::string import_path_;
        std::string subtitle_path_;
        std::string queue_search_;
        std::string project_search_;
        std::string output_search_;
        int library_filter_ = 0;
        bool show_subtitle_import_ = true;
        bool show_diagnostics_ = false;
        std::string ui_status_;
        std::string ui_error_;
    };
}

#endif
