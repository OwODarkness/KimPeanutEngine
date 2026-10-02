#ifndef KPENGINE_EDITOR_UI_GLOW_H
#define KPENGINE_EDITOR_UI_GLOW_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>

namespace kpengine::editor
{
    enum class EditorGlowMarkerKind : uint8_t
    {
        BeginRegion,
        EndRegion,
        PushEmission,
        PopEmission,
    };

    struct EditorGlowMarker
    {
        static constexpr uint32_t kMagic = 0x4544474cU;
        uint32_t magic = kMagic;
        EditorGlowMarkerKind kind = EditorGlowMarkerKind::BeginRegion;
        uint8_t quality = 0;
        uint16_t reserved = 0;
        uint32_t region_id = 0;
        float bounds[4]{};
        float linear_tint[3]{};
        float strength = 0.0f;
        float radius = 0.0f;
    };

    struct EditorGlowEvent
    {
        int draw_list_index = 0;
        int command_index = 0;
        EditorGlowMarker marker;
    };

    struct EditorGlowPacket
    {
        std::vector<EditorGlowEvent> events;
        uint32_t region_count = 0;
    };

    inline void EditorGlowMarkerCallback(const ImDrawList *, const ImDrawCmd *);

    inline bool BuildEditorGlowPacket(const ImDrawData *draw_data,
                                      EditorGlowPacket &packet,
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
        if (draw_data == nullptr)
        {
            return fail("ImGui draw data is unavailable");
        }

        for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index)
        {
            const ImDrawList *const list = draw_data->CmdLists[list_index];
            uint32_t active_region = 0;
            uint32_t emission_depth = 0;
            for (int command_index = 0; command_index < list->CmdBuffer.Size;
                 ++command_index)
            {
                const ImDrawCmd &command = list->CmdBuffer[command_index];
                if (command.UserCallback != EditorGlowMarkerCallback)
                {
                    continue;
                }
                if (command.UserCallbackData == nullptr ||
                    command.UserCallbackDataSize != sizeof(EditorGlowMarker))
                {
                    return fail("Glow marker payload is invalid");
                }
                EditorGlowMarker marker{};
                std::memcpy(&marker, command.UserCallbackData, sizeof(marker));
                if (marker.magic != EditorGlowMarker::kMagic)
                {
                    return fail("Glow marker signature is invalid");
                }

                switch (marker.kind)
                {
                case EditorGlowMarkerKind::BeginRegion:
                    if (active_region != 0 || marker.region_id == 0 ||
                        marker.bounds[2] <= marker.bounds[0] ||
                        marker.bounds[3] <= marker.bounds[1] || marker.radius <= 0.0f ||
                        !std::isfinite(marker.bounds[0]) ||
                        !std::isfinite(marker.bounds[1]) ||
                        !std::isfinite(marker.bounds[2]) ||
                        !std::isfinite(marker.bounds[3]) ||
                        !std::isfinite(marker.radius))
                    {
                        return fail("Glow region boundary is invalid");
                    }
                    active_region = marker.region_id;
                    if (++packet.region_count > 8)
                    {
                        return fail("Glow region budget exceeded");
                    }
                    break;
                case EditorGlowMarkerKind::EndRegion:
                    if (active_region == 0 || marker.region_id != active_region ||
                        emission_depth != 0)
                    {
                        return fail("Glow region end is unmatched");
                    }
                    active_region = 0;
                    break;
                case EditorGlowMarkerKind::PushEmission:
                    if (active_region == 0 || marker.region_id != active_region ||
                        !std::isfinite(marker.strength) || marker.strength < 0.0f ||
                        !std::isfinite(marker.linear_tint[0]) ||
                        !std::isfinite(marker.linear_tint[1]) ||
                        !std::isfinite(marker.linear_tint[2]) ||
                        ++emission_depth > 8)
                    {
                        return fail("Glow emission scope is invalid");
                    }
                    break;
                case EditorGlowMarkerKind::PopEmission:
                    if (active_region == 0 || marker.region_id != active_region ||
                        emission_depth == 0)
                    {
                        return fail("Glow emission pop is unmatched");
                    }
                    --emission_depth;
                    break;
                default:
                    return fail("Glow marker kind is unsupported");
                }
                packet.events.push_back({list_index, command_index, marker});
            }
            if (active_region != 0 || emission_depth != 0)
            {
                return fail("Glow marker stream ended inside a scope");
            }
        }
        return true;
    }

    inline void EditorGlowMarkerCallback(const ImDrawList *, const ImDrawCmd *)
    {
        // Marker payloads are consumed by the Editor RHI adapter. The current
        // native ImGui backends safely treat them as no-op callbacks.
    }

    inline float DecodeGlowSrgb(const uint8_t channel) noexcept
    {
        const float encoded = static_cast<float>(channel) / 255.0f;
        return encoded <= 0.04045f ? encoded / 12.92f
                                   : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    }

    inline void AddEditorGlowMarker(ImDrawList *draw_list,
                                   const EditorGlowMarker &marker)
    {
        if (draw_list == nullptr)
        {
            return;
        }
        EditorGlowMarker payload = marker;
        draw_list->AddCallback(EditorGlowMarkerCallback, &payload, sizeof(payload));
    }

    inline void BeginEditorGlowRegion(ImDrawList *draw_list, const uint32_t region_id,
                                      const ImVec2 &minimum, const ImVec2 &maximum,
                                      const float radius)
    {
        EditorGlowMarker marker{};
        marker.kind = EditorGlowMarkerKind::BeginRegion;
        marker.region_id = region_id;
        marker.bounds[0] = minimum.x;
        marker.bounds[1] = minimum.y;
        marker.bounds[2] = maximum.x;
        marker.bounds[3] = maximum.y;
        marker.radius = std::max(0.0f, radius);
        AddEditorGlowMarker(draw_list, marker);
    }

    inline void EndEditorGlowRegion(ImDrawList *draw_list, const uint32_t region_id)
    {
        EditorGlowMarker marker{};
        marker.kind = EditorGlowMarkerKind::EndRegion;
        marker.region_id = region_id;
        AddEditorGlowMarker(draw_list, marker);
    }

    inline void PushEditorGlowEmission(ImDrawList *draw_list, const uint32_t region_id,
                                       const ImU32 color, const float strength)
    {
        EditorGlowMarker marker{};
        marker.kind = EditorGlowMarkerKind::PushEmission;
        marker.region_id = region_id;
        marker.linear_tint[0] = DecodeGlowSrgb(
            static_cast<uint8_t>((color >> IM_COL32_R_SHIFT) & 0xffU));
        marker.linear_tint[1] = DecodeGlowSrgb(
            static_cast<uint8_t>((color >> IM_COL32_G_SHIFT) & 0xffU));
        marker.linear_tint[2] = DecodeGlowSrgb(
            static_cast<uint8_t>((color >> IM_COL32_B_SHIFT) & 0xffU));
        marker.strength = std::max(0.0f, strength);
        AddEditorGlowMarker(draw_list, marker);
    }

    inline void PopEditorGlowEmission(ImDrawList *draw_list, const uint32_t region_id)
    {
        EditorGlowMarker marker{};
        marker.kind = EditorGlowMarkerKind::PopEmission;
        marker.region_id = region_id;
        AddEditorGlowMarker(draw_list, marker);
    }

    // These helpers mark only the crisp source geometry. The RHI adapter copies
    // and filters that coverage; legacy renderers safely draw the core only.
    inline ImU32 ScaleGlowAlpha(const ImU32 color, const float scale) noexcept
    {
        const auto alpha = static_cast<uint8_t>((color >> IM_COL32_A_SHIFT) & 0xffU);
        const auto scaled = static_cast<uint8_t>(
            std::clamp(scale, 0.0f, 1.0f) * static_cast<float>(alpha));
        return (color & ~(0xffU << IM_COL32_A_SHIFT)) |
               (static_cast<ImU32>(scaled) << IM_COL32_A_SHIFT);
    }

    inline void AddEditorGlowLine(ImDrawList *draw_list, const ImVec2 &start,
                                  const ImVec2 &end, const ImU32 color,
                                  const float thickness = 1.8f,
                                  const uint32_t region_id = 0U)
    {
        if (draw_list == nullptr)
        {
            return;
        }

        PushEditorGlowEmission(draw_list, region_id, color, 1.0f);
        draw_list->AddLine(start, end, color, thickness);
        PopEditorGlowEmission(draw_list, region_id);
    }

    inline void AddEditorGlowText(ImDrawList *draw_list, const ImVec2 &position,
                                  const ImU32 color, const char *text,
                                  const uint32_t region_id)
    {
        if (draw_list == nullptr || text == nullptr || *text == '\0')
        {
            return;
        }

        PushEditorGlowEmission(draw_list, region_id, color, 1.0f);
        draw_list->AddText(position, color, text);
        PopEditorGlowEmission(draw_list, region_id);
    }
}

#endif
