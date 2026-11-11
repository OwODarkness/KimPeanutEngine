#include "debug_viewer_view.h"

#include <algorithm>

#include "graphics/backend/common/render_target.h"
#include "render/render_system.h"

namespace kpengine::render::editor
{
    namespace
    {
        using kpengine::editor::ui::UiCanvasPrimitive;
        using kpengine::editor::ui::UiCanvasPrimitiveKind;
        using kpengine::editor::ui::UiColor;
        using kpengine::editor::ui::UiContext;
        using kpengine::editor::ui::UiImageId;
        using kpengine::editor::ui::UiTextRole;
        using kpengine::editor::ui::UiVec2;

        const UiImageId kDebugImageId = kpengine::editor::ui::StableUiImageId("render.debug_viewer.output");

        struct DebugViewOption
        {
            CaptureView view;
            const char *label;
        };

        constexpr DebugViewOption kDebugViewOptions[] = {
            {CaptureView::SceneColor, "Scene Color"},
            {CaptureView::WorldNormal, "World Normal"},
            {CaptureView::LinearDepth, "Linear Depth"},
            {CaptureView::BaseColor, "Base Color"},
            {CaptureView::MaterialParams, "Material Parameters"},
            {CaptureView::ShadowVisibility, "Shadow Visibility"},
            {CaptureView::SpotShadowDepth, "Spot Shadow Depth"},
            {CaptureView::SpotShadowVisibility, "Spot Shadow Visibility"},
            {CaptureView::PointShadowDepth, "Point Shadow Depth"},
            {CaptureView::PointShadowVisibility, "Point Shadow Visibility"},
            {CaptureView::SelectionMask, "Selection Mask"},
        };

        std::uint64_t ChoiceValue(const CaptureView view)
        {
            return static_cast<std::uint64_t>(view);
        }

        CaptureView ViewFromChoice(const std::uint64_t value)
        {
            for (const DebugViewOption &option : kDebugViewOptions)
                if (ChoiceValue(option.view) == value) return option.view;
            return CaptureView::WorldNormal;
        }

        const char *ViewLabel(const CaptureView view)
        {
            for (const DebugViewOption &option : kDebugViewOptions)
                if (option.view == view) return option.label;
            return "World Normal";
        }
    }

    DebugViewerView::DebugViewerView(RenderSystem *const render_system)
        : render_system_(render_system)
    {
    }

    DebugViewerView::~DebugViewerView()
    {
        SetDemand(false);
    }

    void DebugViewerView::SetDemand(const bool active)
    {
        if (render_system_ != nullptr)
            render_system_->SetDebugViewDemand(
                DebugViewConsumer::EditorDebugViewer,
                active ? std::optional<CaptureView>{debug_view_} : std::nullopt);
    }

    void DebugViewerView::Update(const bool visible)
    {
        visible_ = visible;
        SetDemand(visible);
    }

    void DebugViewerView::Draw(UiContext &context, const ImageSourceSink &register_image)
    {
        if (render_system_ == nullptr)
        {
            context.Text("Render diagnostics are unavailable.", UiTextRole::Muted);
            return;
        }

        const graphics::RenderTargetView target = render_system_->GetDebugRenderTargetView();
        const UiVec2 available = context.AvailableContent();
        const float width = std::max(1.0F, available.x);
        const float height = std::max(72.0F, available.y - 54.0F);
        std::vector<UiCanvasPrimitive> primitives;
        UiCanvasPrimitive image{};
        image.kind = UiCanvasPrimitiveKind::Image;
        image.image = kDebugImageId;

        if (!target.IsValid())
        {
            UiCanvasPrimitive message{};
            message.kind = UiCanvasPrimitiveKind::Text;
            message.first = {0.5F, 0.5F};
            message.color = {0.65F, 0.68F, 0.72F, 1.0F};
            message.text = "Diagnostic preview is not ready.";
            message.text_anchor = {0.5F, 0.5F};
            primitives.push_back(std::move(message));
        }
        else
        {
            if (register_image) register_image(kDebugImageId, target);
            const float aspect = static_cast<float>(target.width) /
                                 static_cast<float>(target.height);
            const float canvas_aspect = width / height;
            const UiVec2 image_size = canvas_aspect > aspect
                ? UiVec2{height * aspect, height}
                : UiVec2{width, width / aspect};
            image.first = {(width - image_size.x) * 0.5F / width,
                           (height - image_size.y) * 0.5F / height};
            image.second = {image.first.x + image_size.x / width,
                            image.first.y + image_size.y / height};
            primitives.push_back(std::move(image));
        }
        context.Canvas({.id = "render.debug_viewer.preview", .label = "Diagnostic Preview"},
                       {width, height}, std::move(primitives));

        const std::vector<kpengine::editor::ui::UiChoice> choices = [&]
        {
            std::vector<kpengine::editor::ui::UiChoice> result;
            result.reserve(std::size(kDebugViewOptions));
            for (const DebugViewOption &option : kDebugViewOptions)
                result.push_back({option.label, option.label, ChoiceValue(option.view)});
            return result;
        }();
        const auto selected = context.Combo<std::uint64_t>(
            {.id = "render.debug_viewer.output", .label = "Output"},
            ChoiceValue(debug_view_), choices);
        if (selected.changed)
        {
            debug_view_ = ViewFromChoice(selected.value);
            if (visible_) SetDemand(true);
        }
        context.Text(ViewLabel(debug_view_), UiTextRole::Muted);
    }
}
