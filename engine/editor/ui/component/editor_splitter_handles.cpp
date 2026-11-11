#include "editor/ui/component/editor_splitter_handles.h"

#include <algorithm>
#include <cfloat>
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

    void EditorSplitterHandles::Render(EditorLayoutModel &model,
                                       const std::span<const EditorRect> occlusions)
    {
        const ImGuiIO &io = ImGui::GetIO();
        ImDrawList *const foreground = ImGui::GetForegroundDrawList();
        drag_just_ended_ = false;
        const bool pointer_valid = std::isfinite(io.MousePos.x) && std::isfinite(io.MousePos.y) &&
                                   io.MousePos.x > -FLT_MAX * 0.5f &&
                                   io.MousePos.y > -FLT_MAX * 0.5f;
        const bool popup_open = ImGui::IsPopupOpen(nullptr,
            ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const auto cancel_drag = [&model, this]()
        {
            if (dragging_ && active_split_ < static_cast<std::size_t>(EditorSplitterId::Count))
                model.SetSplitterFraction(static_cast<EditorSplitterId>(active_split_),
                                         drag_start_fraction_);
            dragging_ = false;
        };

        // Cancel before inspecting the seam. Focus loss in this ImGui revision clears
        // MouseDown without generating a MouseReleased edge.
        if (dragging_ && (io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape) || !pointer_valid ||
            (!io.MouseDown[ImGuiMouseButton_Left] && !ImGui::IsMouseReleased(ImGuiMouseButton_Left))))
            cancel_drag();

        if (!dragging_ && pointer_valid && !popup_open && !io.WantTextInput &&
            !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive() &&
            !IsOccluded(io.MousePos.x, io.MousePos.y, occlusions) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            for (std::size_t index = 0; index < static_cast<std::size_t>(EditorSplitterId::Count); ++index)
            {
                const auto id = static_cast<EditorSplitterId>(index);
                if (!model.IsSplitterDraggable(id)) continue;
                const auto exposed_hit = SubtractOcclusions(
                    ExpandForHit(model.SplitterHandleRect(id, kHandleThicknessPx),
                                 model.SplitterAxis(id)), occlusions);
                const bool hit = std::any_of(exposed_hit.begin(), exposed_hit.end(),
                    [&io](const EditorRect &rect) { return rect.Contains(io.MousePos.x, io.MousePos.y); });
                if (!hit) continue;
                dragging_ = true;
                active_split_ = index;
                drag_start_fraction_ = model.SplitterFraction(id);
                drag_start_mouse_ = AxisValue(io.MousePos, model.SplitterAxis(id));
                break;
            }
        }

        for (std::size_t index = 0; index < static_cast<std::size_t>(EditorSplitterId::Count); ++index)
        {
            const auto id = static_cast<EditorSplitterId>(index);
            if (!model.IsSplitterDraggable(id))
            {
                if (dragging_ && active_split_ == index) cancel_drag();
                continue;
            }
            const EditorLayoutAxis axis = model.SplitterAxis(id);
            const EditorRect handle = model.SplitterHandleRect(id, kHandleThicknessPx);
            if (handle.IsEmpty()) { if (dragging_ && active_split_ == index) cancel_drag(); continue; }
            const EditorRect hit = ExpandForHit(handle, axis);
            const bool hovered = pointer_valid && hit.Contains(io.MousePos.x, io.MousePos.y) &&
                !IsOccluded(io.MousePos.x, io.MousePos.y, occlusions);
            const bool mine = dragging_ && active_split_ == index;
            if (!mine && !hovered) continue;
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

            ImGui::SetMouseCursor(axis == EditorLayoutAxis::Horizontal
                                      ? ImGuiMouseCursor_ResizeEW
                                      : ImGuiMouseCursor_ResizeNS);
            for (const EditorRect &piece : SubtractOcclusions(handle, occlusions))
                DrawHandle(foreground, piece, hovered || mine);
        }
    }
}
