#ifndef  KPENGINE_EDITOR_LOG_COMPONENT_H
#define KPENGINE_EDITOR_LOG_COMPONENT_H

#include <string>
#include <vector>

#include "editor/include/editor_ui_component.h"

namespace kpengine{
    namespace ui{
        class EditorLogComponent: public EditorUIComponent{
        public:
            void Render() override;

            void AddLog(const std::string &log);

        private:
            std::vector<std::string> logs;

        };
    }
}

#endif