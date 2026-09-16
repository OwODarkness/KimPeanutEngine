#include "editor/asset/editor_asset_reference_component.h"

#include <imgui.h>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace kpengine::editor
{
    namespace
    {
        constexpr float kReferenceCardHeight{54.0f};
        constexpr float kReferenceCardWidth{300.0f};
        constexpr float kReferenceColumnGap{48.0f};
        constexpr float kReferenceRowGap{12.0f};
        constexpr float kReferenceCanvasPadding{12.0f};

        ImU32 StateColor(std::string_view state)
        {
            if (state == "Loaded")
            {
                return ImGui::GetColorU32(ImVec4(0.30f, 0.78f, 0.40f, 1.0f));
            }
            if (state == "Archive")
            {
                return ImGui::GetColorU32(ImVec4(0.38f, 0.62f, 0.94f, 1.0f));
            }
            if (state == "Missing")
            {
                return ImGui::GetColorU32(ImVec4(0.90f, 0.36f, 0.32f, 1.0f));
            }
            if (state == "Truncated" || state == "Unknown")
            {
                return ImGui::GetColorU32(ImVec4(0.92f, 0.68f, 0.28f, 1.0f));
            }
            return ImGui::GetColorU32(ImGuiCol_TextDisabled);
        }

        const char *DirectionLabel(AssetReferenceDirection direction)
        {
            return direction == AssetReferenceDirection::Dependencies ? "Dependencies"
                                                                         : "Referencers";
        }

        const char *PresentationLabel(AssetReferencePresentation presentation)
        {
            return presentation == AssetReferencePresentation::Tree ? "Tree" : "Text";
        }

        const char *RowMarker(AssetReferenceRowKind kind)
        {
            switch (kind)
            {
            case AssetReferenceRowKind::Root:
                return "●";
            case AssetReferenceRowKind::Edge:
                return "●";
            case AssetReferenceRowKind::SharedLeaf:
                return "↳";
            case AssetReferenceRowKind::CycleLeaf:
                return "↻";
            case AssetReferenceRowKind::MissingLeaf:
                return "!";
            case AssetReferenceRowKind::UnknownCoverageLeaf:
                return "?";
            case AssetReferenceRowKind::Truncation:
                return "…";
            }
            return "•";
        }

        std::string RowTitle(const AssetReferenceRow &row)
        {
            if (row.kind == AssetReferenceRowKind::UnknownCoverageLeaf ||
                row.kind == AssetReferenceRowKind::Truncation)
            {
                return row.display_name;
            }
            if (row.type_name.empty())
            {
                return row.display_name;
            }
            return row.type_name + "  " + row.display_name;
        }

        std::string RowSubtitle(const AssetReferenceRow &row)
        {
            std::string subtitle = row.state_label;
            if (!row.relation_label.empty())
            {
                subtitle += " · ";
                subtitle += row.relation_label;
            }
            if (!row.annotation.empty())
            {
                subtitle += " · ";
                subtitle += row.annotation;
            }
            return subtitle;
        }

        struct ReferenceTreeLayout
        {
            std::vector<std::size_t> parents;
            std::vector<std::vector<std::size_t>> children;
            std::vector<float> y_positions;
            std::size_t max_depth{};
            float leaf_cursor{};
        };

        ReferenceTreeLayout BuildTreeLayout(const std::vector<AssetReferenceRow> &rows)
        {
            ReferenceTreeLayout layout;
            const std::size_t no_parent = std::numeric_limits<std::size_t>::max();
            layout.parents.assign(rows.size(), no_parent);
            layout.children.resize(rows.size());
            layout.y_positions.assign(rows.size(), 0.0f);

            std::vector<std::size_t> ancestors;
            ancestors.reserve(rows.size());
            for (std::size_t index = 0; index < rows.size(); ++index)
            {
                layout.max_depth = std::max(layout.max_depth,
                                            static_cast<std::size_t>(rows[index].depth));
                while (!ancestors.empty() &&
                       rows[ancestors.back()].depth >= rows[index].depth)
                {
                    ancestors.pop_back();
                }
                if (!ancestors.empty())
                {
                    layout.parents[index] = ancestors.back();
                    layout.children[ancestors.back()].push_back(index);
                }
                ancestors.push_back(index);
            }

            const auto assign = [&](auto &&self, std::size_t index) -> float
            {
                const std::vector<std::size_t> &children = layout.children[index];
                if (children.empty())
                {
                    const float y = layout.leaf_cursor;
                    layout.leaf_cursor += kReferenceCardHeight + kReferenceRowGap;
                    layout.y_positions[index] = y;
                    return y;
                }

                for (const std::size_t child : children)
                {
                    self(self, child);
                }
                const float first = layout.y_positions[children.front()];
                const float last = layout.y_positions[children.back()];
                const float y = (first + last) * 0.5f;
                layout.y_positions[index] = y;
                return y;
            };

            if (!rows.empty())
            {
                assign(assign, 0);
            }
            return layout;
        }
    }

    EditorAssetReferenceComponent::EditorAssetReferenceComponent(
        AssetReferenceViewModel &model, EditorWindowConfig config)
        : EditorWindowComponent("Asset Reference Viewer", config), model_(model)
    {
    }

    void EditorAssetReferenceComponent::SetLocateInBrowser(LocateInBrowser locate_in_browser)
    {
        locate_in_browser_ = std::move(locate_in_browser);
    }

    void EditorAssetReferenceComponent::RenderContent()
    {
        model_.Sync();
        RenderToolbar();
        ImGui::Separator();

        if (model_.RootKey().empty())
        {
            RenderEmptyState();
            return;
        }

        if (model_.Presentation() == AssetReferencePresentation::Tree)
        {
            RenderTree();
        }
        else
        {
            RenderText();
        }
        RenderDetails();
    }

    void EditorAssetReferenceComponent::RenderToolbar()
    {
        // BeginCombo defaults to the entire remaining content width. Both controls are
        // intentionally compact so the presentation toggle stays inside a narrow window.
        constexpr float kDirectionWidth = 132.0f;
        constexpr float kPresentationWidth = 78.0f;
        const char *direction = DirectionLabel(model_.Direction());
        ImGui::SetNextItemWidth(kDirectionWidth);
        if (ImGui::BeginCombo("##reference_direction", direction))
        {
            for (const AssetReferenceDirection candidate :
                 {AssetReferenceDirection::Dependencies, AssetReferenceDirection::Referencers})
            {
                const bool selected = candidate == model_.Direction();
                if (ImGui::Selectable(DirectionLabel(candidate), selected))
                {
                    model_.SetDirection(candidate);
                    copied_ = false;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        const char *presentation = PresentationLabel(model_.Presentation());
        ImGui::SetNextItemWidth(kPresentationWidth);
        if (ImGui::BeginCombo("##reference_presentation", presentation))
        {
            for (const AssetReferencePresentation candidate :
                 {AssetReferencePresentation::Tree, AssetReferencePresentation::Text})
            {
                const bool selected = candidate == model_.Presentation();
                if (ImGui::Selectable(PresentationLabel(candidate), selected))
                {
                    model_.SetPresentation(candidate);
                    copied_ = false;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("Expand All"))
        {
            model_.ExpandAll();
        }
        ImGui::SameLine();
        if (ImGui::Button("Collapse All"))
        {
            model_.CollapseAll();
        }
        ImGui::SameLine();
        const bool can_copy = model_.HasResolvedRoot();
        if (!can_copy)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Copy"))
        {
            const std::string &text = model_.ExportedText();
            if (!text.empty())
            {
                ImGui::SetClipboardText(text.c_str());
                copied_ = true;
            }
        }
        if (!can_copy)
        {
            ImGui::EndDisabled();
        }
        if (copied_)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Copied");
        }

        ImGui::Text("Root: %s", model_.RootKey().empty() ? "<none>" : model_.RootKey().c_str());
        if (locate_in_browser_ && model_.HasResolvedRoot())
        {
            ImGui::SameLine();
            if (ImGui::Button("Locate in Browser"))
            {
                locate_in_browser_(model_.RootKey());
            }
        }
    }

    void EditorAssetReferenceComponent::RenderTree()
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float height = std::max(120.0f, available.y - 86.0f);
        if (!ImGui::BeginChild("##reference_tree", ImVec2(0.0f, height), true,
                               ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::EndChild();
            return;
        }

        // Tree interactions rebuild the model rows; render an immutable frame snapshot.
        const std::vector<AssetReferenceRow> rows = model_.Rows();
        const ReferenceTreeLayout layout = BuildTreeLayout(rows);
        const float column_extent = kReferenceCardWidth + kReferenceColumnGap;
        const float canvas_width =
            kReferenceCanvasPadding * 2.0f + kReferenceCardWidth +
            static_cast<float>(layout.max_depth) * column_extent;
        const float canvas_height = std::max(
            kReferenceCardHeight + kReferenceCanvasPadding * 2.0f,
            kReferenceCanvasPadding * 2.0f +
                std::max(0.0f, layout.leaf_cursor - kReferenceRowGap));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList *const draw = ImGui::GetWindowDrawList();
        const ImU32 connector_color = ImGui::GetColorU32(ImGuiCol_Border);

        // Draw elbow connectors first so every card sits cleanly above its links.
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            const std::size_t parent = layout.parents[index];
            if (parent == std::numeric_limits<std::size_t>::max())
            {
                continue;
            }
            const ImVec2 parent_min(
                origin.x + kReferenceCanvasPadding +
                    static_cast<float>(rows[parent].depth) * column_extent,
                origin.y + kReferenceCanvasPadding + layout.y_positions[parent]);
            const ImVec2 child_min(
                origin.x + kReferenceCanvasPadding +
                    static_cast<float>(rows[index].depth) * column_extent,
                origin.y + kReferenceCanvasPadding + layout.y_positions[index]);
            const float parent_center_y = parent_min.y + kReferenceCardHeight * 0.5f;
            const float child_center_y = child_min.y + kReferenceCardHeight * 0.5f;
            const float elbow_x = parent_min.x + kReferenceCardWidth +
                                  kReferenceColumnGap * 0.5f;
            const ImVec2 connector_min(
                std::min(parent_min.x + kReferenceCardWidth, child_min.x),
                std::min(parent_center_y, child_center_y));
            const ImVec2 connector_max(
                std::max(elbow_x, child_min.x),
                std::max(parent_center_y, child_center_y));
            if (!ImGui::IsRectVisible(connector_min, connector_max))
            {
                continue;
            }
            draw->AddLine(ImVec2(parent_min.x + kReferenceCardWidth, parent_center_y),
                          ImVec2(elbow_x, parent_center_y), connector_color, 1.0f);
            draw->AddLine(ImVec2(elbow_x, parent_center_y),
                          ImVec2(elbow_x, child_center_y), connector_color, 1.0f);
            draw->AddLine(ImVec2(elbow_x, child_center_y),
                          ImVec2(child_min.x, child_center_y), connector_color, 1.0f);
        }

        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            const AssetReferenceRow &row = rows[index];
            const ImVec2 card_min(
                origin.x + kReferenceCanvasPadding +
                    static_cast<float>(row.depth) * column_extent,
                origin.y + kReferenceCanvasPadding + layout.y_positions[index]);
            const ImVec2 card_max(card_min.x + kReferenceCardWidth,
                                  card_min.y + kReferenceCardHeight);
            if (!ImGui::IsRectVisible(card_min, card_max))
            {
                continue;
            }

            ImGui::PushID(row.occurrence_key.c_str());
            ImGui::SetCursorScreenPos(card_min);
            const bool clicked = ImGui::InvisibleButton(
                "##reference_card", ImVec2(kReferenceCardWidth, kReferenceCardHeight),
                ImGuiButtonFlags_MouseButtonLeft);
            const bool hovered = ImGui::IsItemHovered();
            if (clicked)
            {
                const bool on_expand_marker =
                    row.has_children && ImGui::GetIO().MousePos.x < card_min.x + 30.0f;
                if (on_expand_marker)
                {
                    model_.ToggleExpanded(row.occurrence_key);
                }
                else
                {
                    model_.SelectOccurrence(row.occurrence_key);
                    if (row.kind == AssetReferenceRowKind::Edge &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        model_.SetRoot(row.stable_key);
                    }
                }
            }
            const bool selected = model_.SelectedOccurrence() == row.occurrence_key;
            const ImU32 fill = ImGui::GetColorU32(
                selected ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_Header : ImGuiCol_FrameBg);
            draw->AddRectFilled(card_min, card_max, fill, 5.0f);
            draw->AddRect(card_min, card_max, ImGui::GetColorU32(ImGuiCol_Border), 5.0f);
            if (row.has_children)
            {
                draw->AddText(ImVec2(card_min.x + 12.0f, card_min.y + 17.0f),
                              ImGui::GetColorU32(ImGuiCol_Text), row.expanded ? "v" : ">");
            }
            draw->AddCircleFilled(ImVec2(card_min.x + 34.0f, card_min.y + 18.0f), 5.0f,
                                  StateColor(row.state_label));
            draw->AddText(ImVec2(card_min.x + 48.0f, card_min.y + 7.0f),
                          ImGui::GetColorU32(ImGuiCol_Text), RowMarker(row.kind));
            const std::string title = RowTitle(row);
            draw->PushClipRect(ImVec2(card_min.x + 62.0f, card_min.y),
                               ImVec2(card_max.x - 10.0f, card_max.y), true);
            draw->AddText(ImVec2(card_min.x + 62.0f, card_min.y + 7.0f),
                          ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
            const std::string subtitle = RowSubtitle(row);
            draw->AddText(ImVec2(card_min.x + 62.0f, card_min.y + 29.0f),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled), subtitle.c_str());
            draw->PopClipRect();
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(canvas_width, canvas_height));
        ImGui::EndChild();
    }

    void EditorAssetReferenceComponent::RenderText()
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float height = std::max(120.0f, available.y - 86.0f);
        if (ImGui::BeginChild("##reference_text", ImVec2(0.0f, height), true,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::PushTextWrapPos(-1.0f);
            ImGui::TextUnformatted(model_.ExportedText().c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
    }

    void EditorAssetReferenceComponent::RenderDetails()
    {
        const AssetReferenceRow *const row = model_.SelectedRow();
        if (row == nullptr)
        {
            if (!model_.RootDiagnostic().empty())
            {
                ImGui::TextDisabled("%s", model_.RootDiagnostic().c_str());
            }
            return;
        }
        ImGui::Separator();
        ImGui::Text("%s  -  %s  -  %s", RowTitle(*row).c_str(), row->type_name.c_str(),
                    row->state_label.c_str());
        if (!row->relation_token.empty())
        {
            ImGui::TextDisabled("relation: %s", row->relation_token.c_str());
        }
        if (!row->relation_label.empty())
        {
            ImGui::TextDisabled("label: %s", row->relation_label.c_str());
        }
        if (!row->logical_path.empty())
        {
            ImGui::TextDisabled("logical: %s", row->logical_path.c_str());
        }
        if (!row->product_path.empty())
        {
            ImGui::TextDisabled("product: %s", row->product_path.c_str());
        }
        if (!row->stable_key.empty())
        {
            ImGui::TextDisabled("stable key: %s", row->stable_key.c_str());
        }
        if (!row->annotation.empty())
        {
            ImGui::TextDisabled("%s", row->annotation.c_str());
        }
    }

    void EditorAssetReferenceComponent::RenderEmptyState()
    {
        ImGui::TextDisabled("Select an asset in Asset Browser and open its references.");
    }
}
