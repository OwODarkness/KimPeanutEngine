#ifndef KPENGINE_EDITOR_UI_API_UI_CONTROL_VALIDATION_H
#define KPENGINE_EDITOR_UI_API_UI_CONTROL_VALIDATION_H

#include "editor_ui/ui_types.h"

namespace kpengine::editor::ui::detail
{
    void ValidateControlInput(const UiControl &control);
    void ValidateControlResult(const UiControl &control, const UiEditResult &result);
}

#endif
