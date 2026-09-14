#include "editor/asset/editor_asset_browser_component.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>

#include "config/path.h"

namespace kpengine::editor
{
    namespace
    {
        constexpr float kFolderColumnMinWidth = 112.0f;
        constexpr float kContentColumnMinWidth = 220.0f;
        constexpr float kFolderSplitterWidth = 6.0f;
        constexpr float kTileWidth = 144.0f;
        constexpr float kTileHeight = 128.0f;
        constexpr float kTileIconSize = 48.0f;
        // ASCII, not the em dash the plan asks for: AddFontFromFileTTF is called with no
        // glyph ranges, so the atlas holds U+0020..U+00FF only and U+2014 draws as "?".
        // Any non-Latin-1 text in the editor has the same problem.
        constexpr const char *kNoValue = "-";

        // A deterministic badge colour from the type name, so a registered custom type needs
        // no case here and the same type is always the same colour across sessions.
        ImU32 TypeBadgeColor(const std::string &type_name)
        {
            std::uint32_t hash = 2166136261u;
            for (const char c : type_name)
            {
                hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
            }
            const float hue = static_cast<float>(hash % 360u) / 360.0f;
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            ImGui::ColorConvertHSVtoRGB(hue, 0.55f, 0.85f, r, g, b);
            return ImGui::GetColorU32(ImVec4(r, g, b, 1.0f));
        }

        // A document outline: two strokes and a folded corner. Deliberately readable rather
        // than an icon font, which the editor does not load.
        void DrawDocumentGlyph(ImDrawList *draw, const ImVec2 &min, const ImVec2 &max, ImU32 color)
        {
            const float fold = (max.x - min.x) * 0.35f;
            draw->AddRect(min, max, color, 1.0f, 0, 1.2f);
            draw->AddLine(ImVec2(max.x - fold, min.y), ImVec2(max.x, min.y + fold), color, 1.2f);
            draw->AddLine(ImVec2(max.x - fold, min.y), ImVec2(max.x - fold, min.y + fold), color,
                          1.2f);
            draw->AddLine(ImVec2(max.x - fold, min.y + fold), ImVec2(max.x, min.y + fold), color,
                          1.2f);
        }

        enum class AssetIconKind : std::uint8_t
        {
            Document,
            Model,
            Material,
            Texture,
            Level,
        };

        AssetIconKind IconKind(std::string_view type_name)
        {
            std::string folded;
            folded.reserve(type_name.size());
            for (const char c : type_name)
            {
                folded.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
            }
            if (folded == "model") return AssetIconKind::Model;
            if (folded == "material") return AssetIconKind::Material;
            if (folded == "texture") return AssetIconKind::Texture;
            if (folded == "level") return AssetIconKind::Level;
            return AssetIconKind::Document;
        }

        void DrawAssetIcon(ImDrawList *draw, AssetIconKind kind, const ImVec2 &min,
                           const ImVec2 &max, ImU32 color)
        {
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            switch (kind)
            {
            case AssetIconKind::Model:
                draw->AddLine(ImVec2(center.x, min.y), ImVec2(max.x, center.y), color, 2.0f);
                draw->AddLine(ImVec2(max.x, center.y), ImVec2(center.x, max.y), color, 2.0f);
                draw->AddLine(ImVec2(center.x, max.y), ImVec2(min.x, center.y), color, 2.0f);
                draw->AddLine(ImVec2(min.x, center.y), ImVec2(center.x, min.y), color, 2.0f);
                draw->AddLine(ImVec2(center.x, min.y), ImVec2(center.x, max.y), color, 1.5f);
                draw->AddLine(ImVec2(min.x, center.y), ImVec2(max.x, center.y), color, 1.5f);
                break;
            case AssetIconKind::Material:
                draw->AddCircle(center, (max.x - min.x) * 0.38f, color, 20, 2.0f);
                draw->AddCircleFilled(ImVec2(center.x - 7.0f, center.y - 8.0f), 3.0f, color);
                draw->AddLine(ImVec2(center.x - 7.0f, center.y + 10.0f),
                              ImVec2(center.x + 10.0f, center.y + 2.0f), color, 2.0f);
                break;
            case AssetIconKind::Texture:
            {
                draw->AddRect(min, max, color, 3.0f, 0, 2.0f);
                const float cell = (max.x - min.x) / 4.0f;
                for (int y = 0; y < 4; ++y)
                {
                    for (int x = 0; x < 4; ++x)
                    {
                        if ((x + y) % 2 == 0)
                        {
                            draw->AddRectFilled(
                                ImVec2(min.x + x * cell, min.y + y * cell),
                                ImVec2(min.x + (x + 1) * cell, min.y + (y + 1) * cell),
                                color);
                        }
                    }
                }
                break;
            }
            case AssetIconKind::Level:
                draw->AddRect(min, max, color, 2.0f, 0, 2.0f);
                draw->AddLine(ImVec2(min.x + 5.0f, max.y - 8.0f),
                              ImVec2(center.x, min.y + 8.0f), color, 2.0f);
                draw->AddLine(ImVec2(center.x, min.y + 8.0f),
                              ImVec2(max.x - 5.0f, max.y - 8.0f), color, 2.0f);
                draw->AddLine(ImVec2(min.x + 8.0f, max.y - 8.0f),
                              ImVec2(max.x - 8.0f, max.y - 8.0f), color, 2.0f);
                break;
            case AssetIconKind::Document:
                DrawDocumentGlyph(draw, min, max, color);
                break;
            }
        }

        std::string ElideLabel(std::string_view label, float max_width)
        {
            std::string result{label};
            if (ImGui::CalcTextSize(result.c_str()).x <= max_width) return result;
            while (!result.empty() &&
                   ImGui::CalcTextSize((result + "...").c_str()).x > max_width)
            {
                result.pop_back();
            }
            return result.empty() ? "..." : result + "...";
        }

        std::string DisplayFolderPath(std::string_view path)
        {
            if (path.empty())
            {
                return "Content";
            }
            try
            {
                std::filesystem::path display{std::string{path}};
                const std::filesystem::path root =
                    std::filesystem::absolute(project_root).lexically_normal();
                if (display.is_absolute())
                {
                    const std::filesystem::path relative =
                        display.lexically_normal().lexically_relative(root);
                    bool inside_project = !relative.empty();
                    for (const std::filesystem::path &component : relative)
                    {
                        if (component == "..")
                        {
                            inside_project = false;
                            break;
                        }
                    }
                    if (inside_project)
                    {
                        display = relative;
                    }
                }

                std::string result = display.generic_string();
                for (const std::string_view root_name : {std::string_view{"content/"},
                                                         std::string_view{"asset/"}})
                {
                    if (result.rfind(root_name, 0) == 0)
                    {
                        result.erase(0, root_name.size());
                        break;
                    }
                }
                return result.empty() ? "Content" : result;
            }
            catch (...)
            {
                return std::string{path};
            }
        }

        // A tabbed folder, for navigation rather than for an asset.
        void DrawFolderGlyph(ImDrawList *draw, const ImVec2 &min, const ImVec2 &max, ImU32 color)
        {
            const float tab = (max.x - min.x) * 0.4f;
            const float body_top = min.y + (max.y - min.y) * 0.25f;
            draw->AddRectFilled(ImVec2(min.x, min.y), ImVec2(min.x + tab, body_top), color, 1.0f);
            draw->AddRect(ImVec2(min.x, body_top), max, color, 1.0f, 0, 1.2f);
        }

        const char *LocationLabel(AssetBrowserLocation location)
        {
            switch (location)
            {
            case AssetBrowserLocation::All:
                return "All Assets";
            case AssetBrowserLocation::ArchiveProducts:
                return "Archive Products";
            case AssetBrowserLocation::RuntimeOnly:
                return "Runtime Only";
            case AssetBrowserLocation::Missing:
                return "Missing References";
            }
            return "All Assets";
        }
    }

    EditorAssetBrowserComponent::EditorAssetBrowserComponent(AssetBrowserModel &model)
        // No layout slot: the dock host owns this panel's rectangle, exactly as for every
        // other panel it hosts.
        : EditorWindowComponent("Asset Browser", EditorWindowConfig{}), model_(model)
    {
        const std::string &search = model_.Query().search;
        const std::size_t copied = std::min(search.size(), search_.size() - 1);
        std::copy_n(search.begin(), copied, search_.begin());
        search_[copied] = '\0';
    }

    void EditorAssetBrowserComponent::SetOpenReferences(OpenReferences open_references)
    {
        open_references_ = std::move(open_references);
    }

    void EditorAssetBrowserComponent::RenderContent()
    {
        if (!model_.IsSourceAvailable() && !model_.HasSnapshot())
        {
            RenderUnavailable();
            return;
        }

        RenderToolbar();
        ImGui::Separator();

        const bool show_folders = !model_.Folders().empty();
        if (show_folders)
        {
            // Begun and ended unconditionally: BeginChild reports whether the content is
            // visible, not whether the region was pushed, and skipping EndChild when it
            // says no is an ImGui assertion. Three files in this editor have already been
            // fixed for exactly that.
            const float total_width = ImGui::GetContentRegionAvail().x;
            const float child_height =
                std::max(0.0f, ImGui::GetContentRegionAvail().y - 56.0f);
            const float max_folder_width = std::max(
                kFolderColumnMinWidth,
                total_width - kFolderSplitterWidth - kContentColumnMinWidth);
            folder_width_ = std::clamp(folder_width_, kFolderColumnMinWidth,
                                       max_folder_width);
            ImGui::BeginChild("##asset_folders", ImVec2(folder_width_, -56.0f), true);
            RenderFolderList();
            ImGui::EndChild();
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::InvisibleButton("##asset_folder_splitter",
                                  ImVec2(kFolderSplitterWidth, child_height));
            if (ImGui::IsItemActive())
            {
                folder_width_ = std::clamp(folder_width_ + ImGui::GetIO().MouseDelta.x,
                                           kFolderColumnMinWidth, max_folder_width);
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
        }

        ImGui::BeginChild("##asset_rows", ImVec2(0.0f, -56.0f), false);
        if (model_.Presentation() == AssetBrowserPresentation::Table)
        {
            RenderTable();
        }
        else
        {
            RenderTiles();
        }
        ImGui::EndChild();

        RenderDetails();
        RenderStatus();
    }

    void EditorAssetBrowserComponent::RenderUnavailable()
    {
        if (model_.Diagnostic().empty())
        {
            ImGui::TextDisabled("Asset catalog unavailable");
        }
        else
        {
            // The Editor's own message. Asset's diagnostics travel inside the snapshot and
            // are summarised in the status row instead.
            ImGui::TextWrapped("%s", model_.Diagnostic().c_str());
        }
    }

    void EditorAssetBrowserComponent::RenderToolbar()
    {
        const AssetBrowserQuery &query = model_.Query();

        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("##location", LocationLabel(query.location)))
        {
            for (const AssetBrowserLocation candidate :
                 {AssetBrowserLocation::All, AssetBrowserLocation::ArchiveProducts,
                  AssetBrowserLocation::RuntimeOnly, AssetBrowserLocation::Missing})
            {
                if (ImGui::Selectable(LocationLabel(candidate), candidate == query.location))
                {
                    model_.SetLocation(candidate);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::InputTextWithHint("##search", "Search assets...", search_.data(),
                                     search_.size()))
        {
            // Every frame the buffer differs, and the model ignores an unchanged value, so
            // typing filters live without the model rebuilding on a frame that changed
            // nothing.
            model_.SetSearch(search_.data());
        }

        ImGui::SameLine();
        if (ImGui::Button("Table"))
        {
            model_.SetPresentation(AssetBrowserPresentation::Table);
        }
        ImGui::SameLine();
        if (ImGui::Button("Tiles"))
        {
            model_.SetPresentation(AssetBrowserPresentation::CompactTiles);
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
        {
            // Explicit, never automatic: this is the only place besides promotion that
            // captures, and a capture opens the archive.
            (void)model_.Refresh();
        }

        // Rendered only once AB1.3 has bound the callback. An inert control would promise
        // something the editor cannot do yet.
        if (open_references_ && model_.SelectedRow() != nullptr)
        {
            ImGui::SameLine();
            if (ImGui::Button("Open References"))
            {
                open_references_(model_.SelectedRow()->stable_key);
            }
        }
    }

    void EditorAssetBrowserComponent::RenderFolderList()
    {
        ImGui::TextDisabled("Imported Content");
        const AssetBrowserQuery &query = model_.Query();
        if (ImGui::Selectable("All Assets", query.logical_prefix.empty()))
        {
            model_.SetLogicalPrefix({});
        }
        for (const AssetBrowserFolder &folder : model_.Folders())
        {
            ImGui::PushID(folder.path.c_str());
            const std::string label = "    " + DisplayFolderPath(folder.path) + "  (" +
                                      std::to_string(folder.count) + ")";
            if (ImGui::Selectable(label.c_str(), query.logical_prefix == folder.path))
            {
                model_.SetLogicalPrefix(query.logical_prefix == folder.path ? std::string{}
                                                                            : folder.path);
            }
            const ImVec2 item_min = ImGui::GetItemRectMin();
            DrawFolderGlyph(ImGui::GetWindowDrawList(), ImVec2(item_min.x + 4.0f, item_min.y + 4.0f),
                            ImVec2(item_min.x + 16.0f, item_min.y + 16.0f),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::PopID();
        }
    }

    void EditorAssetBrowserComponent::RenderTable()
    {
        const std::vector<AssetBrowserRow> &rows = model_.Rows();
        const AssetBrowserQuery &query = model_.Query();

        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
        if (!ImGui::BeginTable("##asset_table", 5, flags))
        {
            return;
        }

        const auto header = [this, &query](const char *label, AssetBrowserSortColumn column)
        {
            const bool active = query.sort_column == column;
            const std::string text =
                active ? std::string{label} + (query.ascending ? "  ^" : "  v") : label;
            ImGui::TableHeader(text.c_str());
            if (ImGui::IsItemClicked())
            {
                model_.ToggleSort(column);
            }
        };

        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        header("Name", AssetBrowserSortColumn::Name);
        ImGui::TableSetColumnIndex(1);
        header("Type", AssetBrowserSortColumn::Type);
        ImGui::TableSetColumnIndex(2);
        header("State", AssetBrowserSortColumn::Availability);
        ImGui::TableSetColumnIndex(3);
        header("Size", AssetBrowserSortColumn::Size);
        ImGui::TableSetColumnIndex(4);
        header("Path", AssetBrowserSortColumn::Path);

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const AssetBrowserRow &row = rows[static_cast<std::size_t>(index)];
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(index));

                ImGui::TableSetColumnIndex(0);
                // A selectable spanning the row carries the click, and its label is the
                // readable name — the badge beside it is decoration, never the only signal.
                const bool selected = row.stable_key == model_.SelectedKey();
                if (ImGui::Selectable(row.display_name.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowDoubleClick))
                {
                    model_.Select(row.stable_key);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && open_references_)
                    {
                        open_references_(row.stable_key);
                    }
                }
                const ImVec2 badge_min = ImGui::GetItemRectMin();
                DrawDocumentGlyph(ImGui::GetWindowDrawList(),
                                  ImVec2(badge_min.x - 14.0f, badge_min.y + 2.0f),
                                  ImVec2(badge_min.x - 4.0f, badge_min.y + 14.0f),
                                  TypeBadgeColor(row.type_name));

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.type_name.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(row.state_label.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(row.size_label.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(row.logical_path.empty() ? kNoValue
                                                                : row.logical_path.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    void EditorAssetBrowserComponent::RenderTiles()
    {
        const std::vector<AssetBrowserRow> &rows = model_.Rows();
        const float available = ImGui::GetContentRegionAvail().x;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const int column_count = std::max(1, static_cast<int>((available + spacing) /
                                                               (kTileWidth + spacing)));
        const int row_count = static_cast<int>((rows.size() + static_cast<std::size_t>(column_count) - 1) /
                                               static_cast<std::size_t>(column_count));

        ImGuiListClipper clipper;
        clipper.Begin(row_count, kTileHeight + ImGui::GetStyle().ItemSpacing.y);
        while (clipper.Step())
        {
            for (int grid_row = clipper.DisplayStart; grid_row < clipper.DisplayEnd; ++grid_row)
            {
                for (int column = 0; column < column_count; ++column)
                {
                    const std::size_t index = static_cast<std::size_t>(grid_row * column_count + column);
                    if (index >= rows.size()) break;
                    if (column > 0) ImGui::SameLine();

                    const AssetBrowserRow &row = rows[index];
                    ImGui::PushID(row.stable_key.c_str());
                    const bool selected = row.stable_key == model_.SelectedKey();
                    if (ImGui::Selectable("##asset_tile", selected,
                                          ImGuiSelectableFlags_AllowDoubleClick,
                                          ImVec2(kTileWidth, kTileHeight)))
                    {
                        model_.Select(row.stable_key);
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && open_references_)
                        {
                            open_references_(row.stable_key);
                        }
                    }

                    const ImVec2 min = ImGui::GetItemRectMin();
                    const ImVec2 max = ImGui::GetItemRectMax();
                    ImDrawList *const draw = ImGui::GetWindowDrawList();
                    draw->PushClipRect(min, max, true);
                    const ImVec2 icon_min(min.x + (kTileWidth - kTileIconSize) * 0.5f,
                                          min.y + 8.0f);
                    const ImVec2 icon_max(icon_min.x + kTileIconSize,
                                          icon_min.y + kTileIconSize);
                    DrawAssetIcon(draw, IconKind(row.type_name), icon_min, icon_max,
                                  TypeBadgeColor(row.type_name));

                    const std::string name = ElideLabel(row.display_name, kTileWidth - 12.0f);
                    const float name_width = ImGui::CalcTextSize(name.c_str()).x;
                    draw->AddText(ImVec2(min.x + (kTileWidth - name_width) * 0.5f,
                                         min.y + 62.0f),
                                  ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
                    const std::string type = ElideLabel(row.type_name, kTileWidth - 12.0f);
                    const float type_width = ImGui::CalcTextSize(type.c_str()).x;
                    draw->AddText(ImVec2(min.x + (kTileWidth - type_width) * 0.5f,
                                         min.y + 80.0f),
                                  ImGui::GetColorU32(ImGuiCol_TextDisabled), type.c_str());
                    const std::string state = row.state_label + "  " + row.size_label;
                    const float state_width = ImGui::CalcTextSize(state.c_str()).x;
                    draw->AddText(ImVec2(min.x + (kTileWidth - state_width) * 0.5f,
                                         min.y + 98.0f),
                                  ImGui::GetColorU32(ImGuiCol_TextDisabled), state.c_str());
                    draw->PopClipRect();
                    ImGui::PopID();
                }
            }
        }
    }

    void EditorAssetBrowserComponent::RenderDetails()
    {
        const AssetBrowserRow *const row = model_.SelectedRow();
        if (row == nullptr)
        {
            ImGui::TextDisabled("Select an asset to see its details");
            return;
        }

        const AssetBrowserDetails details = model_.SelectedDetails();
        ImGui::Text("%s  -  %s  -  %s", row->display_name.c_str(), row->type_name.c_str(),
                    row->state_label.c_str());
        if (!row->logical_path.empty())
        {
            ImGui::TextDisabled("logical: %s", row->logical_path.c_str());
        }
        if (!row->product_path.empty())
        {
            ImGui::TextDisabled("product: %s", row->product_path.c_str());
        }
        for (const std::string &alias : details.aliases)
        {
            ImGui::TextDisabled("alias: %s", alias.c_str());
        }
        for (const std::string &source : details.source_paths)
        {
            ImGui::TextDisabled("source: %s", source.c_str());
        }
        if (row->kind == asset::AssetCatalogNodeKind::MissingReference)
        {
            ImGui::TextDisabled("an authored reference with nothing behind it");
        }
    }

    void EditorAssetBrowserComponent::RenderStatus()
    {
        ImGui::Separator();
        ImGui::Text("%zu shown / %zu assets | revision %llu", model_.Rows().size(),
                    model_.NodeCount(), static_cast<unsigned long long>(model_.Revision()));
        if (model_.SnapshotWasPartial())
        {
            ImGui::SameLine();
            ImGui::Text("| Partial: %zu warning(s)", model_.WarningCount());
        }
        if (!model_.Diagnostic().empty())
        {
            ImGui::SameLine();
            ImGui::Text("| %s", model_.Diagnostic().c_str());
        }
    }
}
