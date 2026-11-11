#ifndef KPENGINE_UI_IMGUI_INLINE_BACKEND_INTERNAL_H
#define KPENGINE_UI_IMGUI_INLINE_BACKEND_INTERNAL_H

#include "editor_ui/ui_imgui_backend.h"

#include <functional>

struct ImFont;
struct ImDrawList;

namespace kpengine::editor::ui
{
    using UiImageDrawCallback = std::function<void(UiImageId, UiRect, ImDrawList *)>;
    std::unique_ptr<IUiBackend> CreateImGuiInlineBackend(
        ImFont *code_font, UiImageDrawCallback draw_image = {});
}

#endif
