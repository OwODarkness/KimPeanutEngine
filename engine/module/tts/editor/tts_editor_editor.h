#ifndef KPENGINE_MODULE_TTS_EDITOR_EDITOR_H
#define KPENGINE_MODULE_TTS_EDITOR_EDITOR_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "editor/ui/audio_transport_icons.h"
#include "tts_editor_controller.h"

namespace kpengine::editor { class EditorUI; }
namespace kpengine::runtime { class Engine; }

namespace kpengine::tts_editor
{
    class TtsEditorEditor final
    {
        static constexpr std::size_t kAvatarRasterSize = 64;

    public:
        TtsEditorEditor();
        ~TtsEditorEditor();
        bool Initialize(runtime::Engine &engine, TtsEditorController &controller,
                        std::string &diagnostic);
        bool Render(std::string &diagnostic);
        void Shutdown() noexcept;

    private:
        void RenderWorkspace();
        void RenderPreview(const TtsEditorView &view);
        void RenderDialogList(const TtsEditorView &view);
        void RenderSettings(const TtsEditorView &view);
        void RenderTextInput(const TtsEditorView &view);
        void CopySettingsToFields(const TtsEditorSettings &settings);
        TtsEditorSettings FieldsToSettings() const;

        std::unique_ptr<editor::EditorUI> ui_;
        editor::AudioTransportIconMasks transport_icons_;
        std::array<std::uint32_t, kAvatarRasterSize * kAvatarRasterSize> avatar_pixels_{};
        bool avatar_loaded_ = false;
        TtsEditorController *controller_ = nullptr;
        float last_audible_volume_ = 0.8f;
        TtsEditorSettings draft_settings_;
        std::array<char, 4097> text_{};
        std::array<char, 256> address_{};
        std::array<char, 128> api_path_{};
        std::array<char, 128> voice_name_{};
        std::array<char, 1024> ref_audio_path_{};
        std::array<char, 2048> ref_text_{};
        std::array<char, 64> ref_language_{};
        std::array<char, 64> text_language_{};
        std::array<char, 512> output_directory_{};
        std::array<char, 1024> import_path_{};
        std::array<char, 128> export_basename_{};
        int port_ = 0;
        int timeout_seconds_ = 180;
        bool streaming_ = true;
        bool refresh_fields_ = false;
    };
}

#endif
