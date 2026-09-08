#include "module/live2d/editor/live2d_editor_viewer_component.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>

namespace
{
    constexpr float kPreviewAspect = 9.0f / 16.0f;

    struct AspectFitRect
    {
        ImVec2 min{};
        ImVec2 size{};
    };

    AspectFitRect FitAspect(const ImVec2 &available, const ImVec2 &origin,
                            const float aspect)
    {
        if (available.x <= 0.0f || available.y <= 0.0f || aspect <= 0.0f)
        {
            return {origin, {}};
        }

        const float available_aspect = available.x / available.y;
        const ImVec2 size = available_aspect > aspect
                                ? ImVec2(available.y * aspect, available.y)
                                : ImVec2(available.x, available.x / aspect);
        return {
            ImVec2(origin.x + (available.x - size.x) * 0.5f,
                   origin.y + (available.y - size.y) * 0.5f),
            size};
    }

    void DrawPlaceholderCharacter(ImDrawList &draw, const AspectFitRect &canvas,
                                  const float animation_time)
    {
        const ImVec2 center(canvas.min.x + canvas.size.x * 0.5f,
                            canvas.min.y + canvas.size.y * 0.55f);
        const float bob = std::sin(animation_time * 2.0f) * canvas.size.y * 0.0125f;
        const float head_radius = canvas.size.x * 0.17f;
        const float body_width = canvas.size.x * 0.38f;
        const float body_height = canvas.size.y * 0.34f;
        const float shoulder_y = center.y + bob - canvas.size.y * 0.08f;
        const ImVec2 body_min(center.x - body_width * 0.5f, shoulder_y);
        const ImVec2 body_max(center.x + body_width * 0.5f,
                              shoulder_y + body_height);

        const ImU32 hair_color = IM_COL32(72, 52, 104, 255);
        const ImU32 skin_color = IM_COL32(255, 205, 180, 255);
        const ImU32 coat_color = IM_COL32(85, 150, 190, 255);
        const ImU32 accent_color = IM_COL32(255, 190, 80, 255);
        const ImU32 line_color = IM_COL32(35, 38, 52, 255);

        draw.AddCircleFilled(ImVec2(center.x, center.y - canvas.size.y * 0.22f + bob),
                             head_radius, skin_color);
        draw.AddCircleFilled(
            ImVec2(center.x, center.y - canvas.size.y * 0.27f + bob),
            head_radius * 1.08f, hair_color);
        draw.AddCircleFilled(
            ImVec2(center.x, center.y - canvas.size.y * 0.21f + bob),
            head_radius * 0.88f, skin_color);

        const float eye_y = center.y - canvas.size.y * 0.21f + bob;
        draw.AddCircleFilled(ImVec2(center.x - head_radius * 0.34f, eye_y),
                             head_radius * 0.09f, line_color);
        draw.AddCircleFilled(ImVec2(center.x + head_radius * 0.34f, eye_y),
                             head_radius * 0.09f, line_color);
        draw.AddLine(ImVec2(center.x - head_radius * 0.2f,
                            eye_y + head_radius * 0.38f),
                     ImVec2(center.x + head_radius * 0.2f,
                            eye_y + head_radius * 0.38f),
                     line_color, 2.0f);

        draw.AddRectFilled(body_min, body_max, coat_color, body_width * 0.12f);
        draw.AddRectFilled(
            ImVec2(center.x - body_width * 0.07f, shoulder_y),
            ImVec2(center.x + body_width * 0.07f, body_max.y), accent_color);
        draw.AddLine(ImVec2(body_min.x, shoulder_y + body_height * 0.15f),
                     ImVec2(body_min.x - body_width * 0.22f,
                            shoulder_y + body_height * 0.55f),
                     coat_color, body_width * 0.13f);
        draw.AddLine(ImVec2(body_max.x, shoulder_y + body_height * 0.15f),
                     ImVec2(body_max.x + body_width * 0.22f,
                            shoulder_y + body_height * 0.55f),
                     coat_color, body_width * 0.13f);
    }
}

namespace kpengine::live2d::editor
{
    Live2DEditorViewerComponent::Live2DEditorViewerComponent()
        : kpengine::editor::EditorWindowComponent(
              "Live2D Viewer",
              kpengine::editor::EditorWindowConfig{0.8f, 0.0f, 0.2f, 0.35f, true})
    {
    }

    void Live2DEditorViewerComponent::RenderContent()
    {
        ImGui::TextUnformatted("Animated character preview");
        ImGui::SameLine();
        ImGui::TextDisabled("Placeholder");

        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x <= 0.0f || available.y <= 0.0f)
        {
            return;
        }

        ImGui::BeginChild("##Live2DViewerCanvas", available, true,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 canvas_available = ImGui::GetContentRegionAvail();
        const AspectFitRect canvas = FitAspect(
            canvas_available, ImGui::GetCursorScreenPos(), kPreviewAspect);
        if (canvas.size.x > 0.0f && canvas.size.y > 0.0f)
        {
            ImDrawList &draw = *ImGui::GetWindowDrawList();
            const ImU32 background = ImGui::GetColorU32(ImGuiCol_FrameBg);
            const ImU32 grid = ImGui::GetColorU32(ImGuiCol_Border);
            draw.AddRectFilled(canvas.min,
                               ImVec2(canvas.min.x + canvas.size.x,
                                      canvas.min.y + canvas.size.y),
                               background, 4.0f);
            for (int index = 1; index < 4; ++index)
            {
                const float fraction = static_cast<float>(index) / 4.0f;
                draw.AddLine(
                    ImVec2(canvas.min.x + canvas.size.x * fraction, canvas.min.y),
                    ImVec2(canvas.min.x + canvas.size.x * fraction,
                           canvas.min.y + canvas.size.y),
                    grid, 1.0f);
                draw.AddLine(
                    ImVec2(canvas.min.x, canvas.min.y + canvas.size.y * fraction),
                    ImVec2(canvas.min.x + canvas.size.x,
                           canvas.min.y + canvas.size.y * fraction),
                    grid, 1.0f);
            }
            draw.AddRect(canvas.min,
                         ImVec2(canvas.min.x + canvas.size.x,
                                canvas.min.y + canvas.size.y),
                         ImGui::GetColorU32(ImGuiCol_Border), 4.0f);
            DrawPlaceholderCharacter(
                draw, canvas, static_cast<float>(ImGui::GetTime()));
        }
        ImGui::EndChild();

        ImGui::TextDisabled("Aspect Fit  |  9:16  |  Cubism renderer pending");
    }
}
