#ifndef  KPENGINE_EDITOR_LOG_COMPONENT_H
#define KPENGINE_EDITOR_LOG_COMPONENT_H

#include <memory>

#include "editor/include/editor_ui_component.h"

namespace kpengine{
    class LogSystem;
    namespace ui{
        class EditorLogComponent: public EditorUIComponent{
        public:
            //void Initialize(std::shared_ptr<LogSystem> log_system); 
            EditorLogComponent(LogSystem* log_system);
            void Render() override;


        private:
            LogSystem* log_system_;
        };
    }
}

#endif