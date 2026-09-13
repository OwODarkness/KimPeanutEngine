#include "editor/ui/component/editor_tool_row_component.h"

#include <algorithm>

namespace kpengine::editor
{
    namespace
    {
        // Isolated tabs stay in the strip, dimmed, so the View checkmark always has
        // a visible entry to map to and a closed floating window leaves a way back.
        constexpr ImVec4 kIsolatedTabText(0.62f, 0.62f, 0.62f, 1.0f);

        // Drag threshold separating "click to switch" from "drag to isolate". The
        // ImGui default, so a click never resolves as a drag.
        constexpr float kDragThreshold = 6.0f;

        void DrawCloseGlyph(ImDrawList *draw, const ImVec2 &center, float radius, ImU32 color)
        {
            draw->AddLine(ImVec2(center.x - radius, center.y - radius),
                          ImVec2(center.x + radius, center.y + radius), color, 1.6f);
            draw->AddLine(ImVec2(center.x - radius, center.y + radius),
                          ImVec2(center.x + radius, center.y - radius), color, 1.6f);
        }
    }

    EditorToolRowComponent::EditorToolRowComponent(EditorToolRowModel &model,
                                                   EditorWindowConfig config)
        : EditorWindowComponent("Tools", config), model_(model)
    {
    }

    EditorWindowComponent *EditorToolRowComponent::AddPanel(
        std::string id, std::string title, std::unique_ptr<EditorWindowComponent> panel,
        bool open, bool docked)
    {
        const std::size_t index =
            model_.AddEntry(std::move(id), std::move(title), open, docked);

        // Bind the panel to its entry's visibility: one value drives the tab close,
        // the floating window's title X, and the View menu checkmark.
        if (EditorToolRowEntry *const entry = model_.GetEntryMutable(index))
        {
            panel->SetVisibility(&entry->visibility);
        }

        // Keep the parallel vectors index-aligned with the entry store. AddEntry
        // only ever appends, so insertion at the matching index preserves alignment.
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

    void EditorToolRowComponent::Render()
    {
        const std::optional<std::size_t> active = model_.GetActiveIndex();
        const std::vector<std::size_t> detached = model_.GetDetachedIndices();

        // A panel that is not drawn this frame still gets its upkeep, matching the
        // behaviour it had when it ran it unconditionally at the top of Render().
        for (std::size_t index = 0; index < pumps_.size(); ++index)
        {
            const bool drawn = (active.has_value() && *active == index) ||
                               std::find(detached.begin(), detached.end(), index) !=
                                   detached.end();
            if (!drawn && pumps_[index])
            {
                pumps_[index]();
            }
        }

        if (model_.HasVisibleDockedPanel())
        {
            // Base Render() owns the row window, its geometry, and its chrome.
            EditorWindowComponent::Render();
        }
        else
        {
            drag_index_ = -1;
        }

        // Detached windows must open OUTSIDE the row window's Begin/End: ImGui
        // pairs windows with a stack and does not support nesting them.
        RenderDetachedWindows(detached);
    }

    void EditorToolRowComponent::RenderContent()
    {
        // Captured before any child region, so the drag preview compares against the
        // row's own rect rather than a child's.
        const ImVec2 row_min = ImGui::GetWindowPos();
        const ImVec2 row_size = ImGui::GetWindowSize();
        row_rect_ = EditorRect{row_min.x, row_min.y, row_size.x, row_size.y};

        RenderTabStrip();

        const std::optional<std::size_t> active = model_.GetActiveIndex();
        if (!active.has_value() || *active >= panels_.size())
        {
            return;
        }

        ImGui::BeginChild("##tool_row_body", ImVec2(0.0f, 0.0f), false);
        if (panels_[*active] != nullptr)
        {
            panels_[*active]->RenderContent();
        }
        ImGui::EndChild();
    }

    void EditorToolRowComponent::RenderTabStrip()
    {
        const ImGuiStyle &style = ImGui::GetStyle();
        const std::size_t count = model_.GetEntryCount();
        const std::optional<std::size_t> active = model_.GetActiveIndex();
        const float close_w = ImGui::GetFrameHeight();
        const float tab_h = ImGui::GetFrameHeight();

        // Collect the tabs that will actually be drawn first. Emitting SameLine
        // after every entry would leave a dangling one whenever a later entry is
        // closed, and the panel body's BeginChild would then be laid out beside the
        // strip instead of below it — so the row's shape would depend on how many
        // tabs happened to be open.
        std::vector<std::size_t> drawn;
        drawn.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            const EditorToolRowEntry *const entry = model_.GetEntry(index);
            if (entry != nullptr && entry->visibility.IsOpen())
            {
                drawn.push_back(index);
            }
        }

        for (std::size_t slot = 0; slot < drawn.size(); ++slot)
        {
            const std::size_t index = drawn[slot];
            const EditorToolRowEntry *const entry = model_.GetEntry(index);
            if (entry == nullptr)
            {
                continue;
            }
            const bool is_active = active.has_value() && *active == index;
            const bool isolated = !entry->docked;

            ImGui::PushID(static_cast<int>(index));
            const float text_w = ImGui::CalcTextSize(entry->title.c_str()).x;
            const ImVec2 tab_size(
                text_w + style.FramePadding.x * 2.0f + close_w + style.ItemInnerSpacing.x, tab_h);

            int pushed = 0;
            if (is_active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                pushed += 2;
            }
            if (isolated)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, kIsolatedTabText);
                ++pushed;
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

            const bool over_close =
                ImGui::IsMouseHoveringRect(ImVec2(tab_max.x - close_w, tab_min.y), tab_max);
            if (hovered && over_close)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            if (hovered && isolated)
            {
                ImGui::SetTooltip("Isolated - right-click to dock");
            }

            DrawCloseGlyph(ImGui::GetWindowDrawList(),
                           ImVec2(tab_max.x - close_w * 0.5f, tab_min.y + tab_h * 0.5f),
                           tab_h * 0.16f, ImGui::GetColorU32(ImGuiCol_Text));

            if (clicked)
            {
                if (over_close)
                {
                    model_.SetOpen(index, false);
                }
                else
                {
                    model_.SetActiveIndex(index);
                }
            }
            if (item_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, kDragThreshold))
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
                if (context_index_ >= 0)
                {
                    const std::size_t target = static_cast<std::size_t>(context_index_);
                    const bool docked_now = model_.IsDocked(target);
                    if (ImGui::MenuItem(docked_now ? "Isolate" : "Dock to tool row"))
                    {
                        model_.SetDocked(target, !docked_now);
                    }
                }
                if (ImGui::MenuItem("Close tab") && context_index_ >= 0)
                {
                    model_.SetOpen(static_cast<std::size_t>(context_index_), false);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();

            // Only between drawn tabs, never trailing.
            if (slot + 1 < drawn.size())
            {
                ImGui::SameLine();
            }
        }

        RenderDragPreview();
    }

    void EditorToolRowComponent::RenderDragPreview()
    {
        if (drag_index_ < 0)
        {
            return;
        }

        const ImVec2 mouse = ImGui::GetMousePos();
        const EditorToolDropTarget target = ResolveToolRowDrop(row_rect_, mouse.x, mouse.y);
        if (target == EditorToolDropTarget::None)
        {
            return;
        }

        ImDrawList *const foreground = ImGui::GetForegroundDrawList();
        const ImGuiViewport *const viewport = ImGui::GetMainViewport();
        const ImU32 fill = ImGui::GetColorU32(ImGuiCol_NavHighlight, 0.25f);
        const ImU32 border = ImGui::GetColorU32(ImGuiCol_NavHighlight, 0.85f);

        ImVec2 ghost_min;
        ImVec2 ghost_max;
        if (target == EditorToolDropTarget::TabStrip)
        {
            ghost_min = ImVec2(row_rect_.x, row_rect_.y);
            ghost_max = ImVec2(row_rect_.x + row_rect_.width, row_rect_.y + row_rect_.height);
        }
        else
        {
            const ImVec2 ghost_size(viewport->WorkSize.x * 0.45f, viewport->WorkSize.y * 0.35f);
            ghost_min = mouse;
            ghost_max = ImVec2(mouse.x + ghost_size.x, mouse.y + ghost_size.y);
        }

        foreground->AddRectFilled(ghost_min, ghost_max, fill);
        foreground->AddRect(ghost_min, ghost_max, border, 0.0f, 0, 2.0f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

        // Resolve on the global release edge, so a release anywhere on screen ends
        // the gesture rather than depending on which item was last submitted.
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (target == EditorToolDropTarget::TabStrip)
            {
                model_.SetDocked(static_cast<std::size_t>(drag_index_), true);
            }
            else
            {
                model_.SetDocked(static_cast<std::size_t>(drag_index_), false);
            }
            drag_index_ = -1;
        }
    }

    void EditorToolRowComponent::RenderDetachedWindows(const std::vector<std::size_t> &detached)
    {
        const ImGuiViewport *const viewport = ImGui::GetMainViewport();

        for (std::size_t slot = 0; slot < detached.size(); ++slot)
        {
            const std::size_t index = detached[slot];
            const EditorToolRowEntry *const entry = model_.GetEntry(index);
            if (entry == nullptr || index >= panels_.size() || panels_[index] == nullptr)
            {
                continue;
            }

            // Cascade the first-use position so two isolates do not land exactly on
            // top of each other. ImGui's ini remembers wherever the user then puts it.
            const float offset = 80.0f + 24.0f * static_cast<float>(slot);
            ImGui::SetNextWindowPos(
                ImVec2(viewport->WorkPos.x + offset, viewport->WorkPos.y + offset),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(
                ImVec2(viewport->WorkSize.x * 0.45f, viewport->WorkSize.y * 0.35f),
                ImGuiCond_FirstUseEver);

            bool open = true;
            ImGui::Begin(entry->title.c_str(), &open);
            panels_[index]->RenderContent();
            if (ImGui::BeginPopupContextWindow("##tool_row_dock"))
            {
                if (ImGui::MenuItem("Dock to tool row"))
                {
                    model_.SetDocked(index, true);
                }
                ImGui::EndPopup();
            }
            ImGui::End();

            if (!open)
            {
                // The floating window's X drives the same state as the tab close.
                model_.SetOpen(index, false);
            }
        }
    }
}
