#include "editor_docked_workspace.h"

#include <utility>

#include "editor_splitter_handles.h"
#include "editor_tool_row_component.h"
#include "editor/profile/editor_metric.h"
#include "editor/profile/editor_profile_bar.h"
#include "editor_ui/ui_backend.h"

namespace kpengine::editor
{
    namespace
    {
        class ViewerPanel final : public EditorWindowComponent
        {
        public:
            ViewerPanel(const std::string &title, std::function<void(ui::UiContext &)> draw)
                : EditorWindowComponent(title), draw_(std::move(draw)) {}

            void RenderContent(ui::UiContext &context) override { draw_(context); }

        private:
            std::function<void(ui::UiContext &)> draw_;
        };
    }

    struct EditorDockedWorkspace::Impl
    {
        EditorLayoutModel layout;
        EditorToolRowModel model;
        EditorSplitterHandles splitters;
        EditorToolRowComponent host{model, EditorWindowConfig{}};
        std::unique_ptr<EditorProfileBarComponent> profile;
    };

    EditorDockedWorkspace::EditorDockedWorkspace(std::vector<std::unique_ptr<EditorMetric>> metrics)
        : impl_(std::make_unique<Impl>())
    {
        impl_->layout.ResetToThreeColumnTwoRowDefault();
        impl_->host.SetLayoutModel(&impl_->layout);
        if (!metrics.empty())
            impl_->profile = std::make_unique<EditorProfileBarComponent>(std::move(metrics));
    }

    EditorDockedWorkspace::~EditorDockedWorkspace() = default;

    void EditorDockedWorkspace::AddPanel(std::string id, std::string title, const EditorLayoutSlot slot,
                                         std::function<void(ui::UiContext &)> draw)
    {
        auto panel = std::make_unique<ViewerPanel>(title, std::move(draw));
        impl_->host.AddPanel(std::move(id), std::move(title), std::move(panel), true, slot);
    }

    void EditorDockedWorkspace::ShowPanel(const std::string_view id)
    {
        (void)impl_->model.ShowById(id);
    }

    void EditorDockedWorkspace::Render(ui::UiContext &context)
    {
        const auto &viewport = context.Viewport();
        auto &layout = impl_->layout;
        layout.SetFixedExtentPixels(EditorSplitterId::StatusBar,
            impl_->profile ? EditorProfileBarComponent::MeasurePreferredHeightPx() : 0.0F);
        layout.Resolve({viewport.logical_origin.x, viewport.logical_origin.y,
                        viewport.logical_extent.x, viewport.logical_extent.y});
        impl_->host.RenderWithContext(context);
        impl_->splitters.Render(layout);
        if (impl_->profile)
        {
            impl_->profile->ApplyLayout(layout.RectOf(EditorLayoutSlot::ProfileBar));
            impl_->profile->Render();
        }
    }
}
