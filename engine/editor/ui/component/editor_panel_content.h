#ifndef KPENGINE_EDITOR_PANEL_CONTENT_H
#define KPENGINE_EDITOR_PANEL_CONTENT_H

namespace kpengine::editor
{
    namespace ui { class UiContext; }
    enum class EditorPanelCloseDecision
    {
        Accept,
        Defer,
        Cancel,
        NoDecision,
    };

    // Content lifecycle shared by docked tabs and floating-window hosts. Update runs
    // even while hidden so queued work can complete without constructing ImGui widgets.
    class EditorPanelContent
    {
    public:
        virtual ~EditorPanelContent() = default;
        virtual void Update(bool visible) { (void)visible; }
        virtual void DrawContent() = 0;
        virtual void DrawContent(ui::UiContext &context)
        {
            (void)context;
            DrawContent();
        }
        virtual EditorPanelCloseDecision OnCloseRequested()
        {
            return EditorPanelCloseDecision::Accept;
        }
        virtual EditorPanelCloseDecision PollCloseRequest()
        {
            return EditorPanelCloseDecision::NoDecision;
        }
    };
}

#endif // KPENGINE_EDITOR_PANEL_CONTENT_H
