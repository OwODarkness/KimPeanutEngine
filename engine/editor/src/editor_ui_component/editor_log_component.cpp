#include "editor_log_component.h"

namespace kpengine{
    namespace ui{
        void EditorLogComponent::Render()
        {
            ImGui::Begin("Log");
            {
                for(int i = 0;i<logs.size();i++)
                {
                    ImGui::Text(logs[i].c_str());
                }
            }
            ImGui::End();
        }

        void EditorLogComponent::AddLog(const std::string &log)
        {
            logs.push_back(log);
        }
    }
}