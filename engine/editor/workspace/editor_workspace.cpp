#include "editor_workspace.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "editor/ui/api/editor_ui/ui_backend.h"

namespace kpengine::editor
{
    EditorWorkspace::EditorWorkspace(WorkspaceLayoutPreset preset, std::string workspace_id)
        : workspace_id_(std::move(workspace_id)), model_(std::move(preset))
    {}

    std::string EditorWorkspace::MakeId(const std::string_view local_id) const
    {
        return workspace_id_ + "." + std::string{local_id};
    }

    bool EditorWorkspace::AddPanel(std::string id, std::string title, std::string region,
                                   std::function<void(ui::UiContext &)> draw)
    {
        if (!draw || !model_.AddPanel(id, std::move(title), std::move(region))) return false;
        draw_.emplace(std::move(id), std::move(draw));
        return true;
    }

    void EditorWorkspace::ShowPanel(const std::string_view id)
    {
        (void)model_.SetOpen(id, true);
    }

    void EditorWorkspace::DrawPanelActions(ui::UiContext &context, WorkspacePanelState &panel)
    {
        const std::string float_id = MakeId(panel.id + ".float");
        if (context.Button({.id = float_id, .label = "Float"}).activated)
            pending_actions_.push_back({ActionKind::Float, panel.id, {}});
        context.SameLine();
        const std::string close_id = MakeId(panel.id + ".close");
        if (context.Button({.id = close_id, .label = "Close"}).activated)
            pending_actions_.push_back({ActionKind::Close, panel.id, {}});
        if (panel.region)
        {
            std::vector<ui::UiChoice> choices;
            choices.reserve(model_.Preset().regions.size());
            for (const WorkspaceRegion &region : model_.Preset().regions)
                choices.push_back({region.id, region.label, ui::UiValue{region.id}});
            const std::string move_id = MakeId(panel.id + ".move");
            auto moved = context.Combo<std::string>({.id = move_id, .label = "Move to"},
                                                     *panel.region, std::move(choices));
            if (moved.changed)
                pending_actions_.push_back({ActionKind::Move, panel.id, std::move(moved.value)});
        }
        else
        {
            for (const WorkspaceRegion &region : model_.Preset().regions)
            {
                const std::string move_id = MakeId(panel.id + ".dock." + region.id);
                if (context.Button({.id = move_id, .label = "Dock: " + region.label}).activated)
                {
                    pending_actions_.push_back({ActionKind::Move, panel.id, region.id});
                    break;
                }
            }
        }
    }

    void EditorWorkspace::Render(ui::UiContext &context)
    {
        pending_actions_.clear();
        pending_panel_release_ = false;
        pending_panel_id_.clear();
        floating_bounds_.clear();
        const ui::UiViewportInfo &viewport = context.Viewport();
        model_.Resolve({viewport.logical_origin.x, viewport.logical_origin.y,
                        viewport.logical_extent.x, viewport.logical_extent.y});

        for (const WorkspaceRegion &region : model_.Preset().regions)
        {
            const WorkspaceRect *bounds = model_.RegionBounds(region.id);
            if (bounds == nullptr || bounds->width <= 1.0f || bounds->height <= 1.0f) continue;
            const std::string window_id = MakeId("region." + region.id);
            auto window = context.Window({.id = window_id, .label = region.label,
                                          .fixed_geometry = true, .window_no_collapse = true,
                                          .window_no_saved_settings = true,
                                          .window_no_bring_to_front = true},
                {bounds->x, bounds->y},
                {bounds->width, bounds->height});
            if (!window.ContentVisible()) continue;
            const std::string layout_id = MakeId("open_layout." + region.id);
            if (context.Button({.id = layout_id, .label = "Workspace"}).activated)
                layout_open_ = true;
            context.SameLine();

            std::vector<WorkspacePanelState *> members;
            for (WorkspacePanelState &panel : model_.Panels())
                if (panel.open && panel.region && *panel.region == region.id)
                    members.push_back(&panel);
            if (members.empty())
            {
                context.Text("No panels in this region");
                continue;
            }
            for (WorkspacePanelState *panel : members)
            {
                const std::string tab_id = MakeId("tab." + panel->id);
                const bool active = model_.ActivePanel(region.id) == panel->id;
                const float tab_width = std::max(76.0f, static_cast<float>(panel->title.size()) * 8.0f + 24.0f);
                const ui::UiColor color = active ? ui::UiColor{0.14f, 0.23f, 0.27f, 1.0f}
                                                 : ui::UiColor{0.08f, 0.12f, 0.15f, 1.0f};
                std::vector<ui::UiCanvasPrimitive> marks{
                    {.kind = ui::UiCanvasPrimitiveKind::FilledRect,
                     .second = {tab_width, 28.0f}, .color = color},
                    {.kind = ui::UiCanvasPrimitiveKind::Text, .first = {8.0f, 7.0f},
                     .color = {0.65f, 0.9f, 0.94f, 1.0f}, .text = panel->title}};
                const auto tab = context.Canvas({.id = tab_id, .label = panel->title},
                                                {tab_width, 28.0f}, std::move(marks));
                const float px = tab.pointer_position.x, py = tab.pointer_position.y;
                if (tab.pressed)
                {
                    drag_candidate_ = panel->id;
                    drag_press_x_ = px;
                    drag_press_y_ = py;
                    panel_dragging_ = false;
                }
                if (drag_candidate_ == panel->id && tab.dragging &&
                    std::hypot(px - drag_press_x_, py - drag_press_y_) >= 6.0f)
                    panel_dragging_ = true;
                if (drag_candidate_ == panel->id && tab.cancelled)
                {
                    drag_candidate_.clear();
                    panel_dragging_ = false;
                }
                if (drag_candidate_ == panel->id && tab.released)
                {
                    if (!panel_dragging_)
                        pending_actions_.push_back({ActionKind::Select, panel->id, {}});
                    else
                    {
                        pending_panel_release_ = true;
                        pending_panel_id_ = panel->id;
                        panel_release_x_ = px;
                        panel_release_y_ = py;
                    }
                    drag_candidate_.clear();
                    panel_dragging_ = false;
                }
                context.SameLine();
            }
            const std::string_view active_id = model_.ActivePanel(region.id);
            const ui::UiVec2 pointer = context.PointerPosition();
            if (panel_dragging_ && drag_candidate_.size() > 0 && bounds->Contains(pointer.x, pointer.y))
                context.Section("Drop to " + region.label);
            const auto active_panel = std::find_if(members.begin(), members.end(),
                [active_id](const WorkspacePanelState *panel) { return panel->id == active_id; });
            if (active_panel != members.end())
            {
                DrawPanelActions(context, **active_panel);
                if (const auto it = draw_.find((*active_panel)->id); it != draw_.end()) it->second(context);
            }
        }

        for (WorkspacePanelState &panel : model_.Panels())
        {
            if (!panel.open || panel.region) continue;
            const std::string window_id = MakeId("float." + panel.id);
            const auto panel_index = static_cast<float>(&panel - model_.Panels().data());
            const float offset = 30.0f * panel_index;
            const bool has_geometry = panel.floating_bounds.width > 0.0f &&
                                      panel.floating_bounds.height > 0.0f;
            auto window = context.Window({.id = window_id, .label = panel.title,
                                          .preserve_geometry = true}, panel.open,
                {has_geometry ? panel.floating_bounds.x + offset :
                                viewport.logical_origin.x + viewport.logical_extent.x * 0.2f + offset,
                 has_geometry ? panel.floating_bounds.y + offset :
                                viewport.logical_origin.y + viewport.logical_extent.y * 0.2f + offset},
                {has_geometry ? panel.floating_bounds.width : viewport.logical_extent.x * 0.45f,
                 has_geometry ? panel.floating_bounds.height : viewport.logical_extent.y * 0.45f});
            if (const auto rect = context.WindowBounds(window_id))
                floating_bounds_.push_back({rect->min.x, rect->min.y,
                    rect->max.x - rect->min.x, rect->max.y - rect->min.y});
            if (!window.ContentVisible()) continue;
            DrawPanelActions(context, panel);
            if (const auto it = draw_.find(panel.id); it != draw_.end()) it->second(context);
        }

        // Do not submit a closed tool window: ImGui's open flag only controls the
        // close affordance, while the caller owns whether the window is submitted.
        if (layout_open_)
        {
            const std::string layout_id = MakeId("layout");
            auto layout = context.Window({.id = layout_id, .label = "Workspace"},
                layout_open_, {viewport.logical_origin.x + viewport.logical_extent.x * 0.35f,
                               viewport.logical_origin.y + viewport.logical_extent.y * 0.25f},
                {300.0f, 200.0f});
            if (layout.ContentVisible())
            {
                context.Section("SPLITTERS");
                for (const WorkspaceSplit &split : model_.Preset().splits)
                {
                    const double value = split.fraction;
                    const auto [minimum, maximum] = model_.SplitFractionRange(split.id);
                    const std::string id = MakeId("split." + split.id);
                    const auto edit = context.Slider<double>({.id = id, .label = split.id},
                        value, minimum, maximum);
                    if (edit.changed) model_.SetSplitFraction(split.id, static_cast<float>(edit.value));
                }
                context.Section("PANELS");
                for (WorkspacePanelState &panel : model_.Panels())
                {
                    const std::string show_id = MakeId("show." + panel.id);
                    if (!panel.open && context.Button({.id = show_id, .label = "Show: " + panel.title}).activated)
                        pending_actions_.push_back({ActionKind::Show, panel.id, {}});
                }
            }
            if (const auto rect = context.WindowBounds(MakeId("layout")))
                floating_bounds_.push_back({rect->min.x, rect->min.y,
                    rect->max.x - rect->min.x, rect->max.y - rect->min.y});
        }
        if (pending_panel_release_ && std::none_of(floating_bounds_.begin(), floating_bounds_.end(),
            [this](const WorkspaceRect &rect) { return rect.Contains(panel_release_x_, panel_release_y_); }))
        {
            const auto target = std::find_if(model_.Preset().regions.begin(), model_.Preset().regions.end(),
                [this](const WorkspaceRegion &candidate)
                {
                    const WorkspaceRect *rect = model_.RegionBounds(candidate.id);
                    return rect != nullptr && rect->Contains(panel_release_x_, panel_release_y_);
                });
            if (target != model_.Preset().regions.end())
                pending_actions_.push_back({ActionKind::Move, pending_panel_id_, target->id});
            else if (panel_release_x_ >= viewport.logical_origin.x && panel_release_y_ >= viewport.logical_origin.y &&
                     panel_release_x_ <= viewport.logical_origin.x + viewport.logical_extent.x &&
                     panel_release_y_ <= viewport.logical_origin.y + viewport.logical_extent.y)
                pending_actions_.push_back({ActionKind::Float, pending_panel_id_, {}});
        }
        RenderSplitters(context);
        CommitActions();
    }

    void EditorWorkspace::RenderSplitters(ui::UiContext &context)
    {
        const ui::UiVec2 pointer = context.PointerPosition();
        const bool over_float = std::any_of(floating_bounds_.begin(), floating_bounds_.end(),
            [pointer](const WorkspaceRect &rect) { return rect.Contains(pointer.x, pointer.y); });
        for (const WorkspaceSplit &split : model_.Preset().splits)
        {
            const WorkspaceRect *parent = model_.SplitBounds(split.id);
            if (parent == nullptr || parent->IsEmpty()) continue;
            const bool horizontal = split.axis == WorkspaceAxis::Horizontal;
            const float seam = horizontal ? parent->x + parent->width * split.fraction
                                          : parent->y + parent->height * split.fraction;
            const WorkspaceRect handle = horizontal
                ? WorkspaceRect{seam - 4.0f, parent->y, 8.0f, parent->height}
                : WorkspaceRect{parent->x, seam - 4.0f, parent->width, 8.0f};
            std::vector<ui::UiCanvasPrimitive> marks;
            const auto exposed = active_split_ == split.id
                ? std::vector<WorkspaceRect>{handle} : SubtractOcclusions(handle, floating_bounds_);
            for (const WorkspaceRect &piece : exposed)
                marks.push_back({.kind = ui::UiCanvasPrimitiveKind::FilledRect,
                    .first = {piece.x - handle.x, piece.y - handle.y},
                    .second = {piece.x - handle.x + piece.width, piece.y - handle.y + piece.height},
                    .color = {0.05f, 0.77f, 0.88f, 0.95f}});
            const std::string window_id = MakeId("split.window." + split.id);
            const bool disable_input = over_float && active_split_ != split.id;
            auto overlay = context.Window({.id = window_id, .label = "Splitter",
                .enabled = !disable_input, .fixed_geometry = true,
                .window_no_title_bar = true, .window_no_background = true,
                .window_no_border = true, .window_no_collapse = true,
                .window_no_saved_settings = true, .window_no_bring_to_front = true,
                .window_no_inputs = disable_input, .window_padding_zero = true},
                {handle.x, handle.y}, {handle.width, handle.height});
            if (!overlay.ContentVisible()) continue;
            const auto input = context.Canvas({.id = MakeId("split.handle." + split.id),
                .label = "Splitter", .enabled = !disable_input},
                {handle.width, handle.height}, std::move(marks));
            const float extent = horizontal ? parent->width : parent->height;
            if (input.pressed && active_split_.empty())
            {
                active_split_ = split.id;
                split_start_fraction_ = split.fraction;
            }
            if (active_split_ != split.id) continue;
            if (input.cancelled)
            {
                model_.SetSplitFraction(split.id, split_start_fraction_);
                active_split_.clear();
                continue;
            }
            if (input.dragging || input.released)
            {
                const float delta = horizontal ? input.pointer_delta.x : input.pointer_delta.y;
                if (extent > 0.0f)
                    model_.SetSplitFraction(split.id, split.fraction + delta / extent);
            }
            if (input.released) active_split_.clear();
        }
    }

    void EditorWorkspace::CommitActions()
    {
        for (const PendingAction &action : pending_actions_)
        {
            switch (action.kind)
            {
            case ActionKind::Close: model_.SetOpen(action.panel, false); break;
            case ActionKind::Float: model_.Float(action.panel); break;
            case ActionKind::Move: model_.MoveToRegion(action.panel, action.region); break;
            case ActionKind::Show: model_.SetOpen(action.panel, true); break;
            case ActionKind::Select: model_.Select(action.panel); break;
            }
        }
        pending_actions_.clear();
    }
}
