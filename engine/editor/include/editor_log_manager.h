#ifndef KPENGINE_EDITOR_LOG_MANAGER_H
#define KPENGINE_EDITOR_LOG_MANAGER_H

#include <memory>
namespace kpengine
{
    class LogSystem;

    namespace editor
    {
        class EditorLogManager
        {
        public:
            void Initialize(LogSystem* log_system);

            void Tick();

        private:
            std::shared_ptr<class EditorLogComponent> log_ui_;
        };
    }
}

#endif