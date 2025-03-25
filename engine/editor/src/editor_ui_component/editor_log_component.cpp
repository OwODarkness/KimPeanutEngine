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

            EditorLogComponent::EditorLogComponent(LogSystem* log_system):log_system_(log_system){}

        void EditorLogComponent::Render()
        {
            assert(log_system_ != nullptr);
            ImGui::Begin("Log");
            {
                const std::vector<LogInfo>& ref = log_system_->GetLogs();
                for(const LogInfo& info : ref)
                {
                    ImVec4 color(info.color[0], info.color[1], info.color[2], info.color[3]);
                    ImGui::TextColored(color, info.log_text.c_str());
                }

            }
            ImGui::End();
        }


    }
}