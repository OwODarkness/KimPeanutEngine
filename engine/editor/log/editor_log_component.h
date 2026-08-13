#ifndef KPENGINE_EDITOR_LOG_COMPONENT_H
#define KPENGINE_EDITOR_LOG_COMPONENT_H


#include "editor/ui/component/editor_window_component.h"
#include "editor/settings/editor_settings.h"

namespace kpengine{
    class LogSystem;
    namespace editor{
        class EditorLogComponent: public EditorWindowComponent{
        public:
            EditorLogComponent(LogSystem* log_system, const LogLevelColorTable& colors,
                               EditorWindowConfig config = {});
            void RenderContent() override;
        private:
            LogSystem* log_system_;
            LogLevelColorTable colors_;
        };
    }
}

#endif