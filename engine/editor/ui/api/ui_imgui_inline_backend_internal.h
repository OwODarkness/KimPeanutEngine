#ifndef KPENGINE_UI_IMGUI_INLINE_BACKEND_INTERNAL_H
#define KPENGINE_UI_IMGUI_INLINE_BACKEND_INTERNAL_H

#include "editor_ui/ui_imgui_backend.h"

struct ImFont;

namespace kpengine::editor::ui
{
    std::unique_ptr<IUiBackend> CreateImGuiInlineBackend(ImFont *code_font);
}

#endif
