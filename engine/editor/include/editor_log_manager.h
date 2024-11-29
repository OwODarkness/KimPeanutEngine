#ifndef KPENGINE_EDITOR_LOG_MANAGER_H
#define KPENGINE_EDITOR_LOG_MANAGER_H


#include <memory>
#include <string>
namespace kpengine
{
    namespace ui
    {
        class EditorLogComponent;
    }
    namespace editor
{
    class EditorLogManager
    {
    public:
        void Initialize();

        void AddLog(const std::string &log);

        void Tick();

    private:
        std::shared_ptr<ui::EditorLogComponent>  log_ui_;
    };
}
}



#endif