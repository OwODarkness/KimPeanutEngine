#include "editor/ui/component/editor_tool_row_component.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace kpengine::editor
{
    namespace
    {
        // Drag threshold separating "click to switch" from "drag to move the panel". The
        // ImGui default, so a click never resolves as a drag.
        constexpr float kDragThreshold = 6.0f;

        void DrawCloseGlyph(ImDrawList *draw, const ImVec2 &center, float radius, ImU32 color)
        {
            draw->AddLine(ImVec2(center.x - radius, center.y - radius),
                          ImVec2(center.x + radius, center.y + radius), color, 1.6f);
            draw->AddLine(ImVec2(center.x - radius, center.y + radius),
                          ImVec2(center.x + radius, center.y - radius), color, 1.6f);
        }

        void DrawDashedRect(ImDrawList *draw, const ImVec2 &min, const ImVec2 &max, ImU32 color,
                            float dash)
        {
            // A full border rather than a filled rect: the target is a region the panel
            // will fill, so it should read as an outline, not as content already there.
            const auto dashed_line = [draw, color, dash](const ImVec2 &from, const ImVec2 &to)
            {
                const float length = std::sqrt((to.x - from.x) * (to.x - from.x) +
                                               (to.y - from.y) * (to.y - from.y));
                if (length <= 0.0f)
                {
                    return;
                }
                const ImVec2 step((to.x - from.x) / length, (to.y - from.y) / length);
                for (float offset = 0.0f; offset < length; offset += dash * 2.0f)
                {
                    const float end = std::min(offset + dash, length);
                    // 2 px: a 1 px line disappears against the editor's dark background,
                    // which made the first capture of this look like a hole in the layout.
                    draw->AddLine(ImVec2(from.x + step.x * offset, from.y + step.y * offset),
                                  ImVec2(from.x + step.x * end, from.y + step.y * end), color, 2.0f);
                }
            };
            dashed_line(ImVec2(min.x, min.y), ImVec2(max.x, min.y));
            dashed_line(ImVec2(max.x, min.y), ImVec2(max.x, max.y));
            dashed_line(ImVec2(max.x, max.y), ImVec2(min.x, max.y));
            dashed_line(ImVec2(min.x, max.y), ImVec2(min.x, min.y));
        }
    }

    EditorToolRowComponent::EditorToolRowComponent(EditorToolRowModel &model,
                                                   EditorWindowConfig config)
        : EditorWindowComponent("Tools", config), model_(model)
    {
    }

    EditorWindowComponent *EditorToolRowComponent::AddPanel(
        std::string id, std::string title, std::unique_ptr<EditorWindowComponent> panel,
        bool open, EditorLayoutSlot dock)
    {
        const std::size_t index = model_.AddEntry(std::move(id), std::move(title), open, dock);

        // Bind the panel to its entry's visibility: one value drives the tab close, the
        // window's title X, and the View menu checkmark.
        if (EditorToolRowEntry *const entry = model_.GetEntryMutable(index))
        {
            panel->SetVisibility(&entry->visibility);
        }

        // Keep the parallel vectors index-aligned with the entry store. AddEntry only ever
        // appends, so insertion at the matching index preserves alignment.
        panels_.insert(panels_.begin() + static_cast<std::ptrdiff_t>(index), std::move(panel));
        pumps_.insert(pumps_.begin() + static_cast<std::ptrdiff_t>(index), PanelPump{});
        return panels_[index].get();
    }

    void EditorToolRowComponent::SetPanelPump(std::string_view id, PanelPump pump)
    {
        const std::optional<std::size_t> index = model_.IndexOf(id);
        if (index.has_value() && *index < pumps_.size())
        {
            pumps_[*index] = std::move(pump);
        }
    }

    bool EditorToolRowComponent::IsDrawnThisFrame(std::size_t index) const noexcept
    {
        const EditorToolRowEntry *const entry = model_.GetEntry(index);
        if (entry == nullptr || !entry->visibility.IsOpen())
        {
            return false;
        }
        if (!entry->dock.has_value())
        {
            return true;  // floating: always drawn
        }
        const std::optional<std::size_t> active = model_.GetActiveInDock(*entry->dock);
        return active.has_value() && *active == index;
    }

    void EditorToolRowComponent::Render()
    {
        // A panel that is not drawn this frame still gets its upkeep, matching the
        // behaviour it had when it ran it unconditionally at the top of Render().
        for (std::size_t index = 0; index < pumps_.size(); ++index)
        {
            if (!IsDrawnThisFrame(index) && pumps_[index])
            {
                pumps_[index]();
            }
        }

        // One window per occupied dock, then one per floating panel. Every window is
        // opened and closed here, never nested: ImGui pairs windows with a stack.
        for (std::size_t slot = 0; slot < kEditorLayoutSlotCount; ++slot)
        {
            const auto dock = static_cast<EditorLayoutSlot>(slot);
            if (EditorLayoutModel::IsDock(dock))
            {
                RenderDock(dock);
            }
        }
        RenderFloatingPanels();

        // The bottom dock's destination is its REGION, not its window. The window is not
        // submitted once every panel has been dragged out of it, and the place to drop a
        // panel back into must not vanish with it.
        if (layout_ != nullptr && layout_->HasSlot(EditorLayoutSlot::ToolRow))
        {
            row_rect_ = layout_->RectOf(EditorLayoutSlot::ToolRow);
        }

        // Both draw on the foreground list, so ordering is call order: targets first, then
        // the ghost that must land on top of them. Neither is inside a window, because
        // either has to keep working when no dock window was drawn.
        RenderPlacementTargets();
        RenderDragPreview();
    }

    void EditorToolRowComponent::RenderDock(EditorLayoutSlot dock)
    {
        if (layout_ == nullptr || !layout_->HasSlot(dock))
        {
            return;
        }
        const std::vector<std::size_t> members = model_.GetDockMembers(dock);
        if (members.empty())
        {
            return;  // an empty dock is a drop target, drawn by RenderPlacementTargets
        }

        const EditorRect rect = SnapEdgesToPixels(layout_->RectOf(dock));
        // The bottom dock carries the horizontal scrollbar, because long diagnostic lines
        // in the Log must stay reachable and the Log no longer owns a window to carry it.
        const int extra_flags = dock == EditorLayoutSlot::ToolRow
                                    ? ImGuiWindowFlags_HorizontalScrollbar
                                    : 0;

        // Every occupied dock is a row container, including the first panel dropped into
        // an empty dock. This gives the container one lock and makes a later drop naturally
        // join it as another tab.

        ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(rect.width, rect.height), ImGuiCond_Always);
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
            extra_flags;

        // Named by the dock, not by its members: the window identity must survive a tab
        // being closed, or ImGui would discard its state every time the set changed.
        const char *const container = EditorLayoutModel::RegionKey(dock);
        ImGui::Begin(container, nullptr, flags);

        // The lock belongs to the row container, never to an individual tab. Locking it
        // disables drag-out for every member while leaving tab selection and close intact.
        if (RenderLockButton(model_.IsDockLocked(dock)))
        {
            model_.ToggleDockLocked(dock);
        }
        RenderTabStrip(dock, members);

        const std::optional<std::size_t> active = model_.GetActiveInDock(dock);
        if (active.has_value() && *active < panels_.size() && panels_[*active] != nullptr)
        {
            ImGui::BeginChild("##dock_body", ImVec2(0.0f, 0.0f), false);
            panels_[*active]->RenderContent();
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void EditorToolRowComponent::RenderPanelWindow(std::size_t index, const EditorRect &rect,
                                                   bool pinned, int extra_flags)
    {
        const EditorToolRowEntry *const entry = model_.GetEntry(index);
        if (entry == nullptr || index >= panels_.size() || panels_[index] == nullptr)
        {
            return;
        }

        const ImGuiViewport *const viewport = ImGui::GetMainViewport();
        ImGuiWindowFlags flags = extra_flags;
        if (pinned)
        {
            // The layout owns the rectangle, so it is re-pushed every frame, and
            // NoSavedSettings keeps it out of imgui.ini.
            ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(rect.width, rect.height), ImGuiCond_Always);
            flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus;
        }
        else
        {
            // Cascade the first-use position so two floating panels do not land exactly on
            // top of each other. ImGui's ini remembers wherever the user then puts it.
            const float offset = 80.0f + 24.0f * static_cast<float>(index);
            ImGui::SetNextWindowPos(
                ImVec2(viewport->WorkPos.x + offset, viewport->WorkPos.y + offset),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(
                ImVec2(viewport->WorkSize.x * 0.45f, viewport->WorkSize.y * 0.35f),
                ImGuiCond_FirstUseEver);
        }

        // The close X writes into the entry's visibility, never into a local that a
        // teardown could lose. Unlike EditorWindowComponent's default, a hosted panel DOES
        // get a close button: the View menu lists every entry, so a closed one comes back.
        bool open = true;
        ImGui::Begin(entry->title.c_str(), &open, flags);
        if (model_.ConsumeFocusRequest(index))
        {
            ImGui::SetWindowFocus();
        }

        // The title bar is this panel's drag handle. A locked panel has none: the padlock
        // gates starting a drag and nothing else, which is why it cannot disagree with the
        // layout. The close button's square is excluded, or reaching for the X would start
        // a drag instead.
        const ImVec2 win_pos = ImGui::GetWindowPos();
        const float title_h = ImGui::GetFrameHeight();
        const ImVec2 title_min(win_pos.x, win_pos.y);
        const ImVec2 title_max(win_pos.x + ImGui::GetWindowWidth() - title_h, win_pos.y + title_h);
        if (!model_.IsLocked(index) && ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(title_min, title_max) &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, kDragThreshold))
        {
            drag_index_ = static_cast<int>(index);
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        if (model_.IsLocked(index) && ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(title_min, title_max))
        {
            ImGui::SetTooltip("Locked: unlock to drag this panel to another dock");
        }

        // The padlock, drawn over this panel's own title bar while its state lives in the
        // placement model rather than in this component.
        if (RenderLockButton(model_.IsLocked(index)))
        {
            model_.ToggleLockedById(entry->id);
        }

        panels_[index]->RenderContent();
        ImGui::End();

        if (!open)
        {
            model_.SetOpen(index, false);
        }
    }

    void EditorToolRowComponent::RenderFloatingPanels()
    {
        const std::vector<std::size_t> floating = model_.GetFloatingIndices();
        for (const std::size_t index : floating)
        {
            RenderPanelWindow(index, EditorRect{}, /*pinned=*/false, 0);
        }
    }

    void EditorToolRowComponent::RenderTabStrip(EditorLayoutSlot dock,
                                                const std::vector<std::size_t> &members)
    {
        const ImGuiStyle &style = ImGui::GetStyle();
        const std::optional<std::size_t> active = model_.GetActiveInDock(dock);
        const float glyph_w = ImGui::GetFrameHeight();
        const float tab_h = ImGui::GetFrameHeight();

        for (std::size_t slot = 0; slot < members.size(); ++slot)
        {
            const std::size_t index = members[slot];
            const EditorToolRowEntry *const entry = model_.GetEntry(index);
            if (entry == nullptr)
            {
                continue;
            }
            const bool is_active = active.has_value() && *active == index;

            ImGui::PushID(static_cast<int>(index));
            const float text_w = ImGui::CalcTextSize(entry->title.c_str()).x;
            // Tabs expose only their title and close affordance. Locking belongs to the
            // containing row chrome, so a tab never carries an independent lock.
            const ImVec2 tab_size(text_w + style.FramePadding.x * 2.0f + glyph_w +
                                      style.ItemInnerSpacing.x,
                                  tab_h);

            int pushed = 0;
            if (is_active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                pushed += 2;
            }
            const bool clicked = ImGui::Button(entry->title.c_str(), tab_size);

            // Grab every item query before drawing or opening a popup, both of which
            // replace the "last item" state.
            const bool item_active = ImGui::IsItemActive();
            const bool right_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 tab_min = ImGui::GetItemRectMin();
            const ImVec2 tab_max = ImGui::GetItemRectMax();

            ImGui::PopStyleColor(pushed);

            const ImVec2 close_min(tab_max.x - glyph_w, tab_min.y);
            const bool over_close = ImGui::IsMouseHoveringRect(close_min, tab_max);
            if (hovered && over_close)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }

            const ImU32 glyph_color = ImGui::GetColorU32(ImGuiCol_Text);
            DrawCloseGlyph(ImGui::GetWindowDrawList(),
                           ImVec2(tab_max.x - glyph_w * 0.5f, tab_min.y + tab_h * 0.5f),
                           tab_h * 0.16f, glyph_color);

            if (clicked)
            {
                if (over_close)
                {
                    model_.SetOpen(index, false);
                }
                else
                {
                    model_.SetActiveInDock(dock, index);
                }
            }
            if (!model_.IsDockLocked(dock) && item_active &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left, kDragThreshold))
            {
                drag_index_ = static_cast<int>(index);
            }
            if (right_clicked)
            {
                context_index_ = static_cast<int>(index);
                ImGui::OpenPopup("##tool_tab_menu");
            }

            if (ImGui::BeginPopup("##tool_tab_menu"))
            {
                if (ImGui::MenuItem("Float") && context_index_ >= 0)
                {
                    (void)model_.FloatPanel(static_cast<std::size_t>(context_index_));
                }
                if (ImGui::MenuItem("Close tab") && context_index_ >= 0)
                {
                    model_.SetOpen(static_cast<std::size_t>(context_index_), false);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();

            // Only between drawn tabs, never trailing. The old bug was a SameLine emitted
            // from the entry count rather than from the tabs actually drawn.
            if (slot + 1 < members.size())
            {
                ImGui::SameLine();
            }
        }
    }

    void EditorToolRowComponent::RenderPlacementTargets()
    {
        if (layout_ == nullptr)
        {
            return;
        }

        // Which dock the drag is over, so it can be highlighted. Computed here rather than
        // in the preview so the dock the user sees lit up is the one the drop resolves to,
        // from one hit test.
        EditorLayoutSlot hovered = EditorLayoutSlot::Count;
        if (drag_index_ >= 0)
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            hovered = layout_->HitTestDock(mouse.x, mouse.y);
        }

        ImDrawList *const foreground = ImGui::GetForegroundDrawList();

        // Only EMPTY docks are drawn as targets. A dock with panels in it is its own,
        // obvious target — and lighting it up would say "this will be replaced", which is
        // the opposite of what a drop does now.
        const auto draw_target = [&](const EditorRect &rect, bool lit, const char *idle_hint)
        {
            const ImVec2 min(rect.x, rect.y);
            const ImVec2 max(rect.x + rect.width, rect.y + rect.height);

            // A faint fill as well as the outline. The first capture of this feature drew
            // only a 1 px dashed line at half alpha, which was invisible against the window
            // background — an empty region then reads as a rendering hole rather than as a
            // destination, which is the opposite of the point.
            foreground->AddRectFilled(
                min, max, ImGui::GetColorU32(ImGuiCol_NavHighlight, lit ? 0.12f : 0.04f));
            DrawDashedRect(foreground, min, max,
                           ImGui::GetColorU32(lit ? ImGuiCol_NavHighlight : ImGuiCol_Border,
                                              lit ? 1.0f : 0.85f),
                           7.0f);

            // Always labelled, not only mid-drag: the region is empty whether or not a drag
            // is in flight, and an unlabelled empty rectangle in the workspace is
            // indistinguishable from something having failed to draw.
            const char *const hint = lit ? "Drop here" : idle_hint;
            const ImVec2 text = ImGui::CalcTextSize(hint);
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            foreground->AddText(ImVec2(center.x - text.x * 0.5f, center.y - text.y * 0.5f),
                                ImGui::GetColorU32(ImGuiCol_Text, lit ? 1.0f : 0.45f), hint);
        };

        for (std::size_t slot = 0; slot < kEditorLayoutSlotCount; ++slot)
        {
            const auto dock = static_cast<EditorLayoutSlot>(slot);
            if (!EditorLayoutModel::IsDock(dock) || !layout_->HasSlot(dock) ||
                (model_.IsDockOccupied(dock) && drag_index_ < 0))
            {
                continue;
            }
            draw_target(layout_->RectOf(dock), dock == hovered,
                        dock == EditorLayoutSlot::ToolRow
                            ? "Drop a panel here to restore the tool row"
                            : "Drop a panel here");
        }
    }

    void EditorToolRowComponent::RenderDragPreview()
    {
        if (drag_index_ < 0)
        {
            return;
        }

        const ImVec2 mouse = ImGui::GetMousePos();

        // Resolve on the global release edge, BEFORE anything can return early, so a
        // release anywhere on screen always ends the gesture. Ending it only when a target
        // resolved would strand drag_index_: the gesture would stay "in flight" with no
        // ghost, and the next click anywhere would then apply a drop the user never started.
        const EditorPlacementTarget target =
            layout_ != nullptr ? ResolvePlacementDrop(*layout_, mouse.x, mouse.y)
                               : EditorPlacementTarget{};
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            const auto index = static_cast<std::size_t>(drag_index_);
            switch (target.kind)
            {
            case EditorPlacementTargetKind::Dock:
                // A dock takes the panel whether or not it already has members: that is
                // what "if not empty, create a tab" means, and it is why nothing has to be
                // evicted here.
                (void)model_.MoveToDock(index, target.dock);
                break;
            case EditorPlacementTargetKind::Float:
            case EditorPlacementTargetKind::None:
            default:
                (void)model_.FloatPanel(index);
                break;
            }
            drag_index_ = -1;
            return;
        }

        // Nowhere to drop: no ghost, but the gesture stays live so the user can keep
        // moving until they reach a destination.
        if (target.kind == EditorPlacementTargetKind::None)
        {
            return;
        }

        ImDrawList *const foreground = ImGui::GetForegroundDrawList();
        const ImGuiViewport *const viewport = ImGui::GetMainViewport();
        const ImU32 fill = ImGui::GetColorU32(ImGuiCol_NavHighlight, 0.25f);
        const ImU32 border = ImGui::GetColorU32(ImGuiCol_NavHighlight, 0.85f);

        ImVec2 ghost_min;
        ImVec2 ghost_max;
        switch (target.kind)
        {
        case EditorPlacementTargetKind::Dock:
        {
            // The dock's own rectangle, so the preview is the exact area that will take the
            // panel rather than an approximation of it.
            const EditorRect &rect = layout_->RectOf(target.dock);
            ghost_min = ImVec2(rect.x, rect.y);
            ghost_max = ImVec2(rect.x + rect.width, rect.y + rect.height);
            break;
        }
        case EditorPlacementTargetKind::Float:
        case EditorPlacementTargetKind::None:
        default:
        {
            const ImVec2 ghost_size(viewport->WorkSize.x * 0.45f, viewport->WorkSize.y * 0.35f);
            ghost_min = mouse;
            ghost_max = ImVec2(mouse.x + ghost_size.x, mouse.y + ghost_size.y);
            break;
        }
        }

        foreground->AddRectFilled(ghost_min, ghost_max, fill);
        foreground->AddRect(ghost_min, ghost_max, border, 0.0f, 0, 2.0f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
}
