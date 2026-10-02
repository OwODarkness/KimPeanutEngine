#ifndef KPENGINE_EDITOR_UI_FRAME_PACKET_H
#define KPENGINE_EDITOR_UI_FRAME_PACKET_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>

#include "editor/ui/editor_ui_glow.h"

namespace kpengine::editor
{
    void EditorUiBeginSrgbImageCallback(const ImDrawList *, const ImDrawCmd *);
    void EditorUiEndSrgbImageCallback(const ImDrawList *, const ImDrawCmd *);

    struct EditorUiVertex
    {
        float position[2]{};
        float uv[2]{};
        float color[4]{};
    };

    struct EditorUiDrawCommand
    {
        uint64_t texture_id = 0;
        float clip_rect[4]{};
        uint32_t first_index = 0;
        uint32_t index_count = 0;
    };

    struct EditorUiFrameItem
    {
        enum class Kind : uint8_t
        {
            Draw,
            GlowMarker,
            ResetRendererState,
        };

        Kind kind = Kind::Draw;
        uint32_t draw_command_index = 0;
        EditorGlowMarker glow_marker{};
    };

    struct EditorUiFramePacket
    {
        ImVec2 display_position{};
        ImVec2 display_size{};
        ImVec2 framebuffer_scale{1.0f, 1.0f};
        std::vector<EditorUiVertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<EditorUiDrawCommand> draw_commands;
        std::vector<EditorUiFrameItem> ordered_items;
        bool requires_legacy_fallback = false;
    };

    inline bool BuildEditorUiFramePacket(const ImDrawData *draw_data,
                                         EditorUiFramePacket &packet,
                                         std::string *diagnostic = nullptr)
    {
        packet = {};
        const auto fail = [&packet, diagnostic](const char *message)
        {
            packet = {};
            if (diagnostic != nullptr)
            {
                *diagnostic = message;
            }
            return false;
        };
        if (draw_data == nullptr || !std::isfinite(draw_data->DisplayPos.x) ||
            !std::isfinite(draw_data->DisplayPos.y) ||
            !std::isfinite(draw_data->DisplaySize.x) ||
            !std::isfinite(draw_data->DisplaySize.y) ||
            !std::isfinite(draw_data->FramebufferScale.x) ||
            !std::isfinite(draw_data->FramebufferScale.y) ||
            draw_data->DisplaySize.x <= 0.0f ||
            draw_data->DisplaySize.y <= 0.0f || draw_data->FramebufferScale.x <= 0.0f ||
            draw_data->FramebufferScale.y <= 0.0f)
        {
            return fail("ImGui frame dimensions are invalid");
        }

        packet.display_position = draw_data->DisplayPos;
        packet.display_size = draw_data->DisplaySize;
        packet.framebuffer_scale = draw_data->FramebufferScale;
        for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index)
        {
            const ImDrawList *const list = draw_data->CmdLists[list_index];
            const uint32_t vertex_base = static_cast<uint32_t>(packet.vertices.size());
            for (const ImDrawVert &source : list->VtxBuffer)
            {
                EditorUiVertex vertex{};
                vertex.position[0] = source.pos.x;
                vertex.position[1] = source.pos.y;
                vertex.uv[0] = source.uv.x;
                vertex.uv[1] = source.uv.y;
                vertex.color[0] = static_cast<float>((source.col >> IM_COL32_R_SHIFT) & 0xffU) / 255.0f;
                vertex.color[1] = static_cast<float>((source.col >> IM_COL32_G_SHIFT) & 0xffU) / 255.0f;
                vertex.color[2] = static_cast<float>((source.col >> IM_COL32_B_SHIFT) & 0xffU) / 255.0f;
                vertex.color[3] = static_cast<float>((source.col >> IM_COL32_A_SHIFT) & 0xffU) / 255.0f;
                if (!std::isfinite(vertex.position[0]) || !std::isfinite(vertex.position[1]) ||
                    !std::isfinite(vertex.uv[0]) || !std::isfinite(vertex.uv[1]))
                {
                    return fail("ImGui vertex contains a non-finite component");
                }
                packet.vertices.push_back(vertex);
            }

            for (const ImDrawCmd &source : list->CmdBuffer)
            {
                if (source.UserCallback == EditorGlowMarkerCallback)
                {
                    if (source.UserCallbackData == nullptr ||
                        source.UserCallbackDataSize != sizeof(EditorGlowMarker))
                    {
                        return fail("Glow marker payload is invalid");
                    }
                    EditorGlowMarker marker{};
                    std::memcpy(&marker, source.UserCallbackData, sizeof(marker));
                    if (marker.magic != EditorGlowMarker::kMagic)
                    {
                        return fail("Glow marker signature is invalid");
                    }
                    EditorUiFrameItem item{};
                    item.kind = EditorUiFrameItem::Kind::GlowMarker;
                    item.glow_marker = marker;
                    packet.ordered_items.push_back(item);
                    continue;
                }
                if (source.UserCallback == ImDrawCallback_ResetRenderState)
                {
                    EditorUiFrameItem item{};
                    item.kind = EditorUiFrameItem::Kind::ResetRendererState;
                    packet.ordered_items.push_back(item);
                    continue;
                }
                if (source.UserCallback == EditorUiBeginSrgbImageCallback ||
                    source.UserCallback == EditorUiEndSrgbImageCallback)
                {
                    continue;
                }
                if (source.UserCallback != nullptr)
                {
                    packet.requires_legacy_fallback = true;
                    continue;
                }
                if (source.ElemCount == 0)
                {
                    continue;
                }
                if (!std::isfinite(source.ClipRect.x) ||
                    !std::isfinite(source.ClipRect.y) ||
                    !std::isfinite(source.ClipRect.z) ||
                    !std::isfinite(source.ClipRect.w))
                {
                    return fail("ImGui clip rectangle contains a non-finite component");
                }
                if (source.IdxOffset > static_cast<uint32_t>(list->IdxBuffer.Size) ||
                    source.ElemCount > static_cast<uint32_t>(list->IdxBuffer.Size) -
                                           source.IdxOffset ||
                    source.VtxOffset > static_cast<uint32_t>(list->VtxBuffer.Size))
                {
                    return fail("ImGui draw command geometry range is invalid");
                }

                EditorUiDrawCommand command{};
                command.texture_id = static_cast<uint64_t>(source.TextureId);
                command.clip_rect[0] = source.ClipRect.x;
                command.clip_rect[1] = source.ClipRect.y;
                command.clip_rect[2] = source.ClipRect.z;
                command.clip_rect[3] = source.ClipRect.w;
                command.first_index = static_cast<uint32_t>(packet.indices.size());
                command.index_count = source.ElemCount;
                for (uint32_t index = 0; index < source.ElemCount; ++index)
                {
                    const uint32_t local_index =
                        static_cast<uint32_t>(list->IdxBuffer[source.IdxOffset + index]);
                    if (local_index >= static_cast<uint32_t>(list->VtxBuffer.Size) -
                                           source.VtxOffset)
                    {
                        return fail("ImGui index references a vertex outside its draw list");
                    }
                    const uint64_t rebased = static_cast<uint64_t>(vertex_base) +
                                             source.VtxOffset + local_index;
                    if (rebased >= packet.vertices.size())
                    {
                        return fail("ImGui index references a vertex outside its draw list");
                    }
                    packet.indices.push_back(static_cast<uint32_t>(rebased));
                }
                const uint32_t command_index =
                    static_cast<uint32_t>(packet.draw_commands.size());
                packet.draw_commands.push_back(command);
                EditorUiFrameItem item{};
                item.kind = EditorUiFrameItem::Kind::Draw;
                item.draw_command_index = command_index;
                packet.ordered_items.push_back(item);
            }
        }
        EditorGlowPacket glow_packet;
        std::string glow_diagnostic;
        if (!BuildEditorGlowPacket(draw_data, glow_packet, &glow_diagnostic))
        {
            return fail(glow_diagnostic.c_str());
        }
        return true;
    }
}

#endif
