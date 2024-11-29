#include "editor_log_manager.h"

#include "editor/include/editor_ui_component/editor_log_component.h"

namespace kpengine
{
    namespace editor
    {

        void EditorLogManager::Initialize()
        {
            log_ui_ = std::make_shared<ui::EditorLogComponent>();
        }

        void EditorLogManager::AddLog(const std::string &log)
        {
            log_ui_->AddLog(log);
        }

        void EditorLogManager::Tick()
        {
            log_ui_->Render();
        }
    }
}