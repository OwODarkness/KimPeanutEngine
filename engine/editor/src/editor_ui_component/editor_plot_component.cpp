#include "editor_plot_component.h"

namespace kpengine
{
    namespace ui
    {
        EditorPlotComponent::EditorPlotComponent(FuncType func, float begin, float end, float step) : 
        func_(func), 
        begin_(begin), 
        end_(end), 
        step_(step)
        {
            Initialize();
        }

        void EditorPlotComponent::Initialize()
        {
            for (float i = begin_; i - end_ <= 1e-6; i += step_)
            {
                samples.push_back(func_(i));
            }
        }

        void EditorPlotComponent::Render()
        {
            ImGui::PlotLines("function", samples.data(), static_cast<int>(samples.size()));
        }
    }
}