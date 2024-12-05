#include "editor_log_manager.h"

#include "editor/include/editor_ui_component/editor_log_component.h"
#include "runtime/core/system/log_system.h"

namespace kpengine
{
    namespace editor
    {

        void EditorLogManager::Initialize(LogSystem* log_system)
        {
            log_ui_ = std::make_shared<ui::EditorLogComponent>(log_system);
        }

        void EditorLogManager::Tick()
        {
            log_ui_->Render();
        }
    }
}