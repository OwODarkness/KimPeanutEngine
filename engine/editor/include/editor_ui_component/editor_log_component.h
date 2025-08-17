#ifndef KPENGINE_EDITOR_LOG_COMPONENT_H
#define KPENGINE_EDITOR_LOG_COMPONENT_H


#include "editor/include/editor_ui_component/editor_window_component.h"

namespace kpengine{
    class LogSystem;
    namespace ui{
        class EditorLogComponent: public EditorWindowComponent{
        public:
            EditorLogComponent(LogSystem* log_system);
            void RenderContent() override;
        private:
            LogSystem* log_system_;
        };
    }
}

#endif