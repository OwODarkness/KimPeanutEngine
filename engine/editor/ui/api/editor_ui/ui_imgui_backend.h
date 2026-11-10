#ifndef KPENGINE_EDITOR_UI_API_UI_IMGUI_BACKEND_H
#define KPENGINE_EDITOR_UI_API_UI_IMGUI_BACKEND_H

#include "ui_backend.h"

#include <memory>

namespace kpengine::editor::ui
{
    // Uses the ImGui frame supplied by the legacy EditorUI host. It never creates
    // a context, polls input, or submits draw data.
    std::unique_ptr<IUiBackend> CreateImGuiInlineBackend();
}

#endif
