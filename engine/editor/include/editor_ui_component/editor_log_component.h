#ifndef KPENGINE_EDITOR_LOG_COMPONENT_H
#define KPENGINE_EDITOR_LOG_COMPONENT_H

#include <memory>

#include "editor/include/editor_ui_component/editor_window_component.h"

namespace kpengine{
    class LogSystem;
    namespace ui{
        class EditorLogComponent: public EditorWindowComponent{
        public:
            //void Initialize(std::shared_ptr<LogSystem> log_system); 
            EditorLogComponent(LogSystem* log_system);
            void RenderContent() override;
        private:
            LogSystem* log_system_;
        };
    }
}

#endif