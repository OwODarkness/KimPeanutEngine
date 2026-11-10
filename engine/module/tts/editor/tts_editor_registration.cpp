#include <memory>

#include "runtime/host/application_host.h"
#include "runtime/launch_options.h"
#include "tts_editor_host.h"

namespace kpengine::tts_editor
{
    namespace
    {
        const runtime::ApplicationHostRegistration registration{
            {"tts",
             [](const runtime::RuntimeLaunchOptions &) { return std::make_unique<TtsEditorHost>(); },
             runtime::ApplicationHostLayout::SharedWindowPresentation,
             false}};
    }
}
