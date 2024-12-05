#include "editor_log_component.h"

#include <string>
#include <vector>
#include "runtime/core/system/log_system.h"

namespace kpengine{
    namespace ui{
        // void EditorLogComponent::Initialize(std::shared_ptr<LogSystem> log_system)
        // {
        //     log_system_ = log_system;
        // }

            EditorLogComponent::EditorLogComponent(LogSystem* log_system):
            log_system_(log_system)
            {

            }

        void EditorLogComponent::Render()
        {
            assert(log_system_ != nullptr);
            ImGui::Begin("Log");
            {
                const std::vector<std::string>& ref = log_system_->GetLogs();
                for(int i = 0;i<ref.size();i++)
                {
                    std::string text = ref.at(i);
                    ImGui::Text(text.c_str());
                }
            }
            ImGui::End();
        }


    }
}