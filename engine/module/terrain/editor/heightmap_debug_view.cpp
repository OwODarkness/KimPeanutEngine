#include "heightmap_debug_view.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <imgui.h>

#include "product/terrain_core.h"

namespace kpengine::terrain
{
    void HeightmapDebugView::RenderContent(const ScalarField2D *const heightfield) const
    {
        if (heightfield == nullptr || heightfield->Samples().empty())
        {
            ImGui::TextDisabled("Heightfield is unavailable.");
            return;
        }

        const GridDomain2D &domain = heightfield->Domain();
        const std::vector<float> &samples = heightfield->Samples();
        const auto [minimum, maximum] =
            std::minmax_element(samples.begin(), samples.end());
        const float range = *maximum - *minimum;
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float aspect = static_cast<float>(domain.width) /
                             static_cast<float>(domain.height);
        float image_width = available.x;
        float image_height = image_width / aspect;
        if (image_height > available.y)
        {
            image_height = available.y;
            image_width = image_height * aspect;
        }
        image_width = std::max(1.0f, image_width);
        image_height = std::max(1.0f, image_height);

        ImVec2 image_min = ImGui::GetCursorScreenPos();
        image_min.x += std::max(0.0f, (available.x - image_width) * 0.5f);
        const ImVec2 cell_size(image_width / static_cast<float>(domain.width),
                               image_height / static_cast<float>(domain.height));
        ImDrawList *const draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(image_min,
                                 ImVec2(image_min.x + image_width,
                                        image_min.y + image_height),
                                 IM_COL32(16, 18, 22, 255));
        for (std::uint32_t y = 0; y < domain.height; ++y)
        {
            for (std::uint32_t x = 0; x < domain.width; ++x)
            {
                const float value = samples[static_cast<std::size_t>(y) * domain.width + x];
                const float normalized = range > std::numeric_limits<float>::epsilon()
                                             ? (value - *minimum) / range
                                             : 0.5f;
                const auto gray = static_cast<std::uint8_t>(
                    std::lround(std::clamp(normalized, 0.0f, 1.0f) * 255.0f));
                const ImVec2 cell_min(image_min.x + static_cast<float>(x) * cell_size.x,
                                      image_min.y + static_cast<float>(y) * cell_size.y);
                const ImVec2 cell_max(cell_min.x + cell_size.x + 0.5f,
                                      cell_min.y + cell_size.y + 0.5f);
                draw_list->AddRectFilled(cell_min, cell_max,
                                         IM_COL32(gray, gray, gray, 255));
            }
        }
        ImGui::Dummy(ImVec2(available.x, image_height));
        ImGui::Text("%u x %u  |  %.2f m to %.2f m", domain.width,
                    domain.height, static_cast<double>(*minimum),
                    static_cast<double>(*maximum));
    }
}
