#include "module/module_registration.h"

#include <memory>
#include <stdexcept>
#include <string>

#include "runtime/engine.h"
#include "tts_editor_host.h"

namespace kpengine::tts_editor
{
    namespace
    {
        void RegisterTtsEditor(runtime::Engine &engine)
        {
            if (engine.GetApplicationMode() != runtime::ApplicationMode::TtsEditor)
                return;
            std::string diagnostic;
            if (!engine.RegisterApplicationHostProvider(
                    runtime::ApplicationMode::TtsEditor,
                    [](runtime::Engine &) { return std::make_unique<TtsEditorHost>(); },
                    diagnostic))
                throw std::runtime_error("TTS editor registration failed: " + diagnostic);
        }

        const module::ModuleRegistration registration(&RegisterTtsEditor);
    }
}
