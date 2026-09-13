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

        // The bar occupies the bottom strip of the layout. Its height is content-derived
        // (font height plus window padding), which is why the layout's root split is a
        // fixed-pixel split rather than a fraction.
        static float MeasurePreferredHeightPx() noexcept;

        std::optional<EditorLayoutSlot> GetLayoutSlot() const noexcept override;
        void ApplyLayout(std::optional<EditorRect> rect) noexcept override;

    private:
        std::vector<std::unique_ptr<EditorMetric>> metrics_;
        std::optional<EditorRect> layout_rect_;
    };
}

#endif // KPENGINE_EDITOR_PROFILE_BAR_H
