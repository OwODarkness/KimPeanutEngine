#ifndef KPENGINE_EDITOR_PLOT_COMPONENT_H
#define KPENGINE_EDITOR_PLOT_COMPONENT_H

#include <functional>

#include "editor/include/editor_ui_component.h"

namespace kpengine{
    namespace ui{
        using FuncType =  std::function<float(float)>;
        class EditorPlotComponent: public EditorUIComponent
        {
        public:
            EditorPlotComponent(FuncType func, float begin, float end, float step = 0.05f);
            virtual void Render() override;
            void Initialize();
        private:
            std::vector<float> samples;
            FuncType func_;
            float begin_;
            float end_;
            float step_;
        };
    }
}

#endif