#ifndef KPENGINE_EDITOR_WORKSPACE_H
#define KPENGINE_EDITOR_WORKSPACE_H

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "editor_workspace_model.h"

namespace kpengine::editor::ui { class UiContext; }

namespace kpengine::editor
{
    class EditorWorkspace final
    {
    public:
        explicit EditorWorkspace(WorkspaceLayoutPreset preset, std::string workspace_id = "workspace");
        bool AddPanel(std::string id, std::string title, std::string region,
                      std::function<void(ui::UiContext &)> draw);
        void ShowPanel(std::string_view id);
        void Render(ui::UiContext &context);
        EditorWorkspaceModel &Model() noexcept { return model_; }

    private:
        enum class ActionKind { Close, Float, Move, Show, Select };
        struct PendingAction { ActionKind kind; std::string panel; std::string region; };
        void DrawPanelActions(ui::UiContext &context, WorkspacePanelState &panel);
        void RenderSplitters(ui::UiContext &context);
        void CommitActions();
        std::string MakeId(std::string_view local_id) const;
        std::string workspace_id_;
        EditorWorkspaceModel model_;
        std::unordered_map<std::string, std::function<void(ui::UiContext &)>> draw_;
        bool layout_open_ = false;
        std::vector<PendingAction> pending_actions_;
        std::vector<WorkspaceRect> floating_bounds_;
        std::string drag_candidate_;
        bool panel_dragging_ = false;
        float drag_press_x_ = 0.0f;
        float drag_press_y_ = 0.0f;
        bool pending_panel_release_ = false;
        std::string pending_panel_id_;
        float panel_release_x_ = 0.0f;
        float panel_release_y_ = 0.0f;
        std::string active_split_;
        float split_start_fraction_ = 0.0f;
    };
}

#endif
