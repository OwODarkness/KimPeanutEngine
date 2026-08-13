#ifndef KPENGINE_EDITOR_PROFILE_BAR_H
#define KPENGINE_EDITOR_PROFILE_BAR_H

#include <memory>
#include <vector>
#include "editor/ui/component/editor_ui_component.h"

namespace kpengine::editor
{
    class EditorMetric;

    // Bottom status bar: samples a list of injected metrics and draws them in one row.
    // Decoupled from the engine/OS — it only ever talks to EditorMetric.
    class EditorProfileBarComponent : public EditorUIComponent
    {
    public:
        explicit EditorProfileBarComponent(std::vector<std::unique_ptr<EditorMetric>> metrics);

        void Render() override;

    private:
        std::vector<std::unique_ptr<EditorMetric>> metrics_;
    };
}

#endif // KPENGINE_EDITOR_PROFILE_BAR_H
