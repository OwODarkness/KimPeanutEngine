#include "editor/asset/editor_asset_browser_component.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "config/path.h"
#include "image_io/image_io.h"

namespace kpengine::editor
{
    namespace
    {
        constexpr float kFolderColumnMinWidth = 112.0f;
        constexpr float kContentColumnMinWidth = 220.0f;
        constexpr float kFolderSplitterWidth = 6.0f;
        constexpr float kTileWidth = 144.0f;
        constexpr float kTileHeight = 84.0f;
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

        constexpr std::size_t kIconRasterSize = 32;

        const char *IconFileName(AssetIconKind kind)
        {
            switch (kind)
            {
            case AssetIconKind::Model:
                return "icon-model.png";
            case AssetIconKind::Material:
                return "icon-material.png";
            case AssetIconKind::Texture:
                return "icon-texture.png";
            case AssetIconKind::Level:
            case AssetIconKind::Document:
                return "icon-file.png";
            }
            return "icon-file.png";
        }

        using IconFileNameMap = std::unordered_map<std::string, std::string>;

        std::string FoldIconType(std::string_view type_name)
        {
            std::string folded;
            folded.reserve(type_name.size());
            for (const char c : type_name)
            {
                folded.push_back(c >= 'A' && c <= 'Z'
                                     ? static_cast<char>(c - 'A' + 'a')
                                     : c);
            }
            return folded;
        }

        std::string BuiltInIconType(std::string_view type_name)
        {
            const std::string folded = FoldIconType(type_name);
            for (const std::string_view candidate : {std::string_view{"model"},
                                                       std::string_view{"material"},
                                                       std::string_view{"texture"},
                                                       std::string_view{"level"}})
            {
                if (folded.find(candidate) != std::string::npos)
                {
                    return std::string{candidate};
                }
            }
            return {};
        }
        IconFileNameMap DefaultIconFileNames()
        {
            return {{"document", IconFileName(AssetIconKind::Document)},
                    {"model", IconFileName(AssetIconKind::Model)},
                    {"material", IconFileName(AssetIconKind::Material)},
                    {"texture", IconFileName(AssetIconKind::Texture)},
                    {"level", IconFileName(AssetIconKind::Level)}};
        }
        bool IsSafeIconFileName(std::string_view file_name)
        {
            const std::filesystem::path path{std::string{file_name}};
            return !file_name.empty() && !path.is_absolute() && path.parent_path().empty();
        }
        std::filesystem::path FindIconPath(const char *file_name)
        {
            for (const std::string_view directory : {std::string_view{"resource/icon"},
                                                       std::string_view{"resouce/icon"}})
            {
                const std::filesystem::path candidate = project_root / directory / file_name;
                std::error_code error;
                if (std::filesystem::is_regular_file(candidate, error) && !error)
                {
                    return candidate;
                }
            }
            return {};
        }

        void LoadIconSettings(IconFileNameMap &file_names)
        {
            std::filesystem::path settings_path = FindIconPath("setting.json");
            if (settings_path.empty())
            {
                settings_path = FindIconPath("settings.json");
            }
            if (settings_path.empty())
            {
                return;
            }

            std::ifstream file(settings_path, std::ios::binary);
            if (!file.is_open())
            {
                return;
            }

            try
            {
                const nlohmann::json root = nlohmann::json::parse(file);
                const nlohmann::json *icons = &root;
                const auto nested_icons = root.find("icons");
                if (nested_icons != root.end())
                {
                    if (!nested_icons->is_object())
                    {
                        return;
                    }
                    icons = &*nested_icons;
                }
                if (!icons->is_object())
                {
                    return;
                }
                for (const auto &[type_name, icon_node] : icons->items())
                {
                    const std::string folded_type = FoldIconType(type_name);
                    if (folded_type.empty() || !icon_node.is_string())
                    {
                        continue;
                    }
                    const std::string file_name = icon_node.get<std::string>();
                    if (IsSafeIconFileName(file_name))
                    {
                        file_names[folded_type] = file_name;
                    }
                }
            }
            catch (const std::exception &)
            {
                // Optional presentation settings fall back to the built-in icon names.
            }
        }
        std::vector<std::uint32_t> DecodeIcon(const std::filesystem::path &path)
        {
            const image_io::ImageDecodeResult decoded =
                image_io::DecodeImageFile(path.generic_string());
            if (!decoded.result.success ||
                decoded.image.format != image_io::ImagePixelFormat::Rgba8 ||
                !decoded.image.IsValid())
            {
                return {};
            }

            std::vector<std::uint32_t> pixels(kIconRasterSize * kIconRasterSize);
            for (std::size_t y = 0; y < kIconRasterSize; ++y)
            {
                const std::size_t source_y =
                    (kIconRasterSize - 1 - y) * decoded.image.height / kIconRasterSize;
                for (std::size_t x = 0; x < kIconRasterSize; ++x)
                {
                    const std::size_t source_x = x * decoded.image.width / kIconRasterSize;
                    const std::size_t source_offset =
                        (source_y * decoded.image.width + source_x) * 4;
                    const std::uint8_t red = decoded.image.pixels[source_offset];
                    const std::uint8_t green = decoded.image.pixels[source_offset + 1];
                    const std::uint8_t blue = decoded.image.pixels[source_offset + 2];
                    const std::uint8_t alpha = decoded.image.pixels[source_offset + 3];
                    pixels[y * kIconRasterSize + x] = IM_COL32(red, green, blue, alpha);
                }
            }
            return pixels;
        }

        void DrawRasterIcon(ImDrawList *draw, const std::vector<std::uint32_t> &pixels,
                            const ImVec2 &min, const ImVec2 &max, ImU32 tint)
        {
            if (pixels.size() != kIconRasterSize * kIconRasterSize)
            {
                return;
            }
            const float cell_width = (max.x - min.x) / static_cast<float>(kIconRasterSize);
            const float cell_height = (max.y - min.y) / static_cast<float>(kIconRasterSize);
            for (std::size_t y = 0; y < kIconRasterSize; ++y)
            {
                for (std::size_t x = 0; x < kIconRasterSize; ++x)
                {
                    const ImU32 source = pixels[y * kIconRasterSize + x];
                    const ImU32 source_alpha = (source >> 24u) & 0xffu;
                    if (source_alpha == 0u)
                    {
                        continue;
                    }
                    // The supplied icons are black alpha masks; tint them for the dark editor.
                    const ImU32 tint_alpha = (tint >> 24u) & 0xffu;
                    const ImU32 alpha = source_alpha * tint_alpha / 255u;
                    const ImU32 color = (tint & 0x00ffffffu) | (alpha << 24u);
                    const ImVec2 cell_min(min.x + static_cast<float>(x) * cell_width,
                                          min.y + static_cast<float>(y) * cell_height);
                    const ImVec2 cell_max(min.x + static_cast<float>(x + 1) * cell_width,
                                          min.y + static_cast<float>(y + 1) * cell_height);
                    draw->AddRectFilled(cell_min, cell_max, color);
                }
            }
        }

        std::string PrefixedAssetName(const AssetBrowserRow &row)
        {
            std::string base = row.display_name;
            if (base.empty() && !row.logical_path.empty())
            {
                base = std::filesystem::path(row.logical_path).filename().string();
            }
            if (base.empty())
            {
                base = "Asset";
            }

            std::string folded_type;
            folded_type.reserve(row.type_name.size());
            for (const char c : row.type_name)
            {
                folded_type.push_back(c >= 'A' && c <= 'Z'
                                          ? static_cast<char>(c - 'A' + 'a')
                                          : c);
            }
            std::string prefix;
            if (folded_type == "material")
            {
                prefix = "Mat_";
            }
            else if (folded_type == "texture")
            {
                prefix = "Tex_";
            }
            else if (folded_type == "model")
            {
                prefix = "Model_";
            }
            else if (folded_type == "level")
            {
                prefix = "Level_";
            }
            if (!prefix.empty() && base.rfind(prefix, 0) != 0)
            {
                base.insert(0, prefix);
            }
            return base;
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
        LoadIconImages();
        const std::string &search = model_.Query().search;
        const std::size_t copied = std::min(search.size(), search_.size() - 1);
        std::copy_n(search.begin(), copied, search_.begin());
        search_[copied] = '\0';
    }

    void EditorAssetBrowserComponent::LoadIconImages()
    {
        icon_pixels_.clear();
        IconFileNameMap file_names = DefaultIconFileNames();
        LoadIconSettings(file_names);
        for (const auto &[type_name, file_name] : file_names)
        {
            const std::filesystem::path path = FindIconPath(file_name.c_str());
            if (path.empty())
            {
                continue;
            }
            std::vector<std::uint32_t> pixels = DecodeIcon(path);
            if (!pixels.empty())
            {
                icon_pixels_[type_name] = std::move(pixels);
            }
        }
    }
    void EditorAssetBrowserComponent::SetOpenReferences(OpenReferences open_references)
    {
        open_references_ = std::move(open_references);
    }

    void EditorAssetBrowserComponent::RenderContent()
    {
        if (!initial_refresh_attempted_)
        {
            initial_refresh_attempted_ = true;
            // Logical-prefix filtering belonged to the old folder-navigation behavior.
            // Clear any stale state so opening this panel always starts with all assets.
            if (!model_.Query().logical_prefix.empty())
            {
                model_.SetLogicalPrefix({});
            }
            if (model_.IsSourceAvailable() &&
                (!model_.HasSnapshot() || model_.NodeCount() == 0))
            {
                // Promotion can precede the first completed catalog publication. Retry once
                // when the user actually opens this panel, not on every frame.
                (void)model_.Refresh();
            }
        }
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

            // Keep the asset child on the same row as the folder pane. Without this
            // explicit continuation, ImGui starts the next child on a new line at the
            // bottom of the folder child, leaving only a clipped sliver interactive.
            ImGui::SameLine(0.0f, 0.0f);
        }

        const float content_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        ImGui::BeginChild("##asset_rows", ImVec2(content_width, -56.0f), false);
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
            LoadIconImages();
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
        if (ImGui::Selectable("All Assets", selected_folder_.empty()))
        {
            selected_folder_.clear();
        }
        for (const AssetBrowserFolder &folder : model_.Folders())
        {
            ImGui::PushID(folder.path.c_str());
            const std::string label = "    " + DisplayFolderPath(folder.path) + "  (" +
                                      std::to_string(folder.count) + ")";
            if (ImGui::Selectable(label.c_str(), selected_folder_ == folder.path))
            {
                selected_folder_ = folder.path;
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
                const std::string display_name = PrefixedAssetName(row);
                const bool selected = row.stable_key == model_.SelectedKey();
                if (ImGui::Selectable(display_name.c_str(), selected,
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
                    const AssetIconKind icon_kind = IconKind(row.type_name);
                    auto icon = icon_pixels_.find(FoldIconType(row.type_name));
                    if (icon == icon_pixels_.end() && !row.logical_path.empty())
                    {
                        const std::size_t slash = row.logical_path.find('/');
                        const std::string path_type = row.logical_path.substr(0, slash);
                        icon = icon_pixels_.find(FoldIconType(path_type));
                    }
                    if (icon == icon_pixels_.end())
                    {
                        const std::string built_in_type = BuiltInIconType(row.type_name);
                        if (!built_in_type.empty())
                        {
                            icon = icon_pixels_.find(built_in_type);
                        }
                    }
                    if (icon == icon_pixels_.end())
                    {
                        DrawAssetIcon(draw, icon_kind, icon_min, icon_max,
                                      TypeBadgeColor(row.type_name));
                    }
                    else
                    {
                        DrawRasterIcon(draw, icon->second, icon_min, icon_max,
                                       TypeBadgeColor(row.type_name));
                    }

                    const std::string name =
                        ElideLabel(PrefixedAssetName(row), kTileWidth - 12.0f);
                    const float name_width = ImGui::CalcTextSize(name.c_str()).x;
                    draw->AddText(ImVec2(min.x + (kTileWidth - name_width) * 0.5f,
                                         min.y + 62.0f),
                                  ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
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
        const std::string display_name = PrefixedAssetName(*row);
        ImGui::Text("%s  -  %s  -  %s", display_name.c_str(), row->type_name.c_str(),
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
