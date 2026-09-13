#include "editor/ui/component/editor_splitter_handles.h"

#include <cmath>
#include <imgui.h>

namespace kpengine::editor
{
    namespace
    {
        // Visual thickness of the drawn strip; the hit area is wider so the seam is easy
        // to grab without the strip becoming visible over panel content.
        constexpr float kHandleThicknessPx = 4.0f;
        constexpr float kHandleHitPaddingPx = 3.0f;

        float AxisValue(const ImVec2 &point, EditorLayoutAxis axis) noexcept
        {
            return axis == EditorLayoutAxis::Horizontal ? point.x : point.y;
        }

        EditorRect ExpandForHit(const EditorRect &rect, EditorLayoutAxis axis) noexcept
        {
            if (axis == EditorLayoutAxis::Horizontal)
            {
                return EditorRect{rect.x - kHandleHitPaddingPx, rect.y,
                                  rect.width + kHandleHitPaddingPx * 2.0f, rect.height};
            }
            return EditorRect{rect.x, rect.y - kHandleHitPaddingPx, rect.width,
                              rect.height + kHandleHitPaddingPx * 2.0f};
        }

        void DrawHandle(ImDrawList *draw, const EditorRect &rect, bool highlighted) noexcept
        {
            const ImU32 color = highlighted ? ImGui::GetColorU32(ImGuiCol_NavHighlight)
                                            : ImGui::GetColorU32(ImGuiCol_Border);
            draw->AddRectFilled(ImVec2(rect.x, rect.y),
                               ImVec2(rect.x + rect.width, rect.y + rect.height), color);
        }
    }

    bool EditorSplitterHandles::ConsumeDragJustEnded() noexcept
    {
        const bool ended = drag_just_ended_;
        drag_just_ended_ = false;
        return ended;
    }

    void EditorSplitterHandles::Render(EditorLayoutModel &model)
    {
        const ImGuiIO &io = ImGui::GetIO();
        ImDrawList *const foreground = ImGui::GetForegroundDrawList();

        // A drag already in flight owns the mouse, so these guards must not be able to
        // take it back. Otherwise a seam may only be grabbed where no real widget is
        // under the cursor: over the tool row's tab strip, the console's input, or a tree
        // row, the PANEL wins and the seam does not steal the click. That precedence is
        // what lets a handle overlay panel content without breaking it.
        const bool free_to_start =
            !io.WantTextInput && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen(nullptr,
                                ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

        drag_just_ended_ = false;

        for (std::size_t index = 0;
             index < static_cast<std::size_t>(EditorSplitterId::Count); ++index)
        {
            const auto id = static_cast<EditorSplitterId>(index);
            if (!model.IsSplitterDraggable(id))
            {
                continue;
            }

            const EditorLayoutAxis axis = model.SplitterAxis(id);
            const EditorRect handle = model.SplitterHandleRect(id, kHandleThicknessPx);
            if (handle.IsEmpty())
            {
                continue;
            }

            const EditorRect hit = ExpandForHit(handle, axis);
            const bool hovered = hit.Contains(io.MousePos.x, io.MousePos.y);
            const bool mine = dragging_ && active_split_ == index;

            if (hovered && !mine && free_to_start &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                dragging_ = true;
                active_split_ = index;
                // The fraction at drag START, so the delta is measured from a fixed base:
                // the model was already resolved this frame, and ApplySplitterDrag reads
                // the parent extent from that resolution.
                drag_start_fraction_ = model.SplitterFraction(id);
                drag_start_mouse_ = AxisValue(io.MousePos, axis);
            }

            if (mine)
            {
                const float delta = AxisValue(io.MousePos, axis) - drag_start_mouse_;
                model.ApplySplitterDrag(id, drag_start_fraction_, delta);

                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    dragging_ = false;
                    drag_just_ended_ = true;
                }
            }

            if (hovered || mine)
            {
                ImGui::SetMouseCursor(axis == EditorLayoutAxis::Horizontal
                                          ? ImGuiMouseCursor_ResizeEW
                                          : ImGuiMouseCursor_ResizeNS);
            }
            DrawHandle(foreground, handle, hovered || mine);
        }
    }
}
