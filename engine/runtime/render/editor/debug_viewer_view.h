#ifndef KPENGINE_RENDER_EDITOR_DEBUG_VIEWER_VIEW_H
#define KPENGINE_RENDER_EDITOR_DEBUG_VIEWER_VIEW_H

#include <functional>

#include "editor_ui/ui_backend.h"
#include "render/render_capture_service.h"

namespace kpengine::graphics
{
    struct RenderTargetView;
}

namespace kpengine::render
{
    class RenderSystem;
}

namespace kpengine::render::editor
{
    class DebugViewerView final
    {
    public:
        using ImageSourceSink = std::function<void(kpengine::editor::ui::UiImageId,
                                                   const graphics::RenderTargetView &)>;

        explicit DebugViewerView(RenderSystem *render_system);
        ~DebugViewerView();

        void Update(bool visible);
        void Draw(kpengine::editor::ui::UiContext &context, const ImageSourceSink &register_image);

    private:
        void SetDemand(bool active);

        RenderSystem *render_system_ = nullptr;
        CaptureView debug_view_ = CaptureView::WorldNormal;
        bool visible_ = false;
    };
}

#endif
