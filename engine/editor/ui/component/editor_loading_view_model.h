#ifndef KPENGINE_EDITOR_LOADING_VIEW_MODEL_H
#define KPENGINE_EDITOR_LOADING_VIEW_MODEL_H

#include <cstdint>
#include <string>

#include "runtime/runtime_startup.h"

namespace kpengine::editor
{
    struct EditorLoadingViewModel
    {
        uint64_t revision = 0;
        bool determinate = false;
        bool ready = false;
        bool failed = false;
        float fraction = -1.0f;
        std::string stage_label;
        std::string current_item;
        std::string counts_label;
        std::string diagnostic;
    };

    EditorLoadingViewModel BuildEditorLoadingViewModel(
        const runtime::StartupSnapshot &snapshot);
}

#endif // KPENGINE_EDITOR_LOADING_VIEW_MODEL_H
