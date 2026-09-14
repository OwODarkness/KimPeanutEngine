#include "editor/asset/asset_browser_model.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace kpengine::editor
{
    namespace
    {
        constexpr std::string_view kUnavailableDiagnostic = "Asset catalog unavailable";

        // ASCII only, and deliberately not std::tolower: that takes a locale and would make
        // matching depend on it. Bytes at or above 0x80 are left alone so UTF-8 text is
        // compared bytewise.
        char FoldByte(char c) noexcept
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        // A missing size sorts before a known one ascending and after it descending, which
        // is the plan's rule. Both directions put real sizes next to each other. This is the
        // one column that applies its own direction, so the caller must not re-orient it.
        int CompareSize(bool lhs_known, bool rhs_known, std::uint64_t lhs_bytes,
                        std::uint64_t rhs_bytes, bool ascending)
        {
            if (lhs_known != rhs_known)
            {
                const bool missing_first = !lhs_known;
                return (missing_first == ascending) ? -1 : 1;
            }
            if (!lhs_known || lhs_bytes == rhs_bytes)
            {
                return 0;
            }
            return (lhs_bytes < rhs_bytes) == ascending ? -1 : 1;
        }

        // Bytewise, and stable: the tie-breakers below are always ascending so a single
        // ordering exists for every possible set of rows.
        int CompareBytes(std::string_view lhs, std::string_view rhs) noexcept
        {
            if (lhs == rhs)
            {
                return 0;
            }
            return lhs < rhs ? -1 : 1;
        }

        bool StartsWith(std::string_view text, std::string_view prefix) noexcept
        {
            return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
        }

        // Every ancestor prefix of a logical path, so "a/b/c" contributes "a" and "a/b".
        // Component-based rather than a substring search, so "a/bc" is not counted under
        // "a/b".
        void CollectFolderPrefixes(std::string_view logical_path,
                                   std::vector<std::string> &out)
        {
            out.clear();
            std::size_t start = 0;
            while (true)
            {
                const std::size_t slash = logical_path.find('/', start);
                if (slash == std::string_view::npos || slash == start)
                {
                    return;
                }
                out.emplace_back(logical_path.substr(0, slash));
                start = slash + 1;
            }
        }

        bool ContainsAnyType(const std::vector<std::string> &included,
                            const std::string &type_name)
        {
            if (included.empty())
            {
                return true;
            }
            return std::find(included.begin(), included.end(), type_name) != included.end();
        }

        bool ContainsAnyAvailability(
            const std::vector<asset::AssetCatalogAvailability> &included,
            asset::AssetCatalogAvailability availability)
        {
            if (included.empty())
            {
                return true;
            }
            return std::find(included.begin(), included.end(), availability) != included.end();
        }
    }

    std::string FoldAscii(std::string_view text)
    {
        std::string folded;
        folded.reserve(text.size());
        for (const char c : text)
        {
            folded.push_back(FoldByte(c));
        }
        return folded;
    }

    std::string FormatAssetByteSize(const std::optional<std::uint64_t> &bytes)
    {
        if (!bytes.has_value())
        {
            // A dash, not "0 B": an unknown size and a zero-byte product are different facts
            // and the view must not conflate them. ASCII rather than the em dash the stage
            // plan names, because the font atlas is loaded with ImGui's default glyph range
            // (U+0020..U+00FF) and draws U+2014 as "?".
            return "-";
        }

        static constexpr std::array<const char *, 5> kUnits{"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(*bytes);
        std::size_t unit = 0;
        while (value >= 1024.0 && unit + 1 < kUnits.size())
        {
            value /= 1024.0;
            ++unit;
        }

        char buffer[32]{};
        if (unit == 0)
        {
            std::snprintf(buffer, sizeof(buffer), "%llu B",
                          static_cast<unsigned long long>(*bytes));
        }
        else if (value >= 100.0)
        {
            std::snprintf(buffer, sizeof(buffer), "%.0f %s", value, kUnits[unit]);
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, kUnits[unit]);
        }
        return std::string{buffer};
    }

    std::string_view AssetBrowserModel::StateLabel(
        asset::AssetCatalogAvailability availability) noexcept
    {
        switch (availability)
        {
        case asset::AssetCatalogAvailability::LoadedArchiveProduct:
            return "Loaded";
        case asset::AssetCatalogAvailability::ArchiveOnly:
            return "Archive";
        case asset::AssetCatalogAvailability::RuntimeOnly:
            return "Runtime";
        case asset::AssetCatalogAvailability::Missing:
            return "Missing";
        }
        return "Runtime";
    }

    bool AssetBrowserModel::IsArchiveProduct(
        asset::AssetCatalogAvailability availability) noexcept
    {
        return availability == asset::AssetCatalogAvailability::ArchiveOnly ||
               availability == asset::AssetCatalogAvailability::LoadedArchiveProduct;
    }

    void AssetBrowserModel::SetSource(asset::IAssetCatalogSnapshotSource *source) noexcept
    {
        source_ = source;
        if (source_ == nullptr)
        {
            diagnostic_ = std::string{kUnavailableDiagnostic};
        }
        else if (diagnostic_ == kUnavailableDiagnostic)
        {
            // Clearing a stale unavailable message, but keeping any real refresh failure:
            // a source arriving does not mean the last capture worked.
            diagnostic_.clear();
        }
    }

    bool AssetBrowserModel::Refresh()
    {
        if (source_ == nullptr)
        {
            diagnostic_ = std::string{kUnavailableDiagnostic};
            return false;
        }

        asset::AssetCatalogSnapshot captured;
        try
        {
            captured = source_->CaptureAssetCatalog();
        }
        catch (const std::exception &e)
        {
            // The last valid snapshot stays, and so does the selection: a failed refresh
            // must not look like an empty catalog.
            diagnostic_ = std::string{"Asset catalog refresh failed: "} + e.what();
            return false;
        }
        catch (...)
        {
            diagnostic_ = "Asset catalog refresh failed";
            return false;
        }

        // The one property this model depends on, since a stable key IS the selection
        // identity. See the header for why Asset's own validator is not re-run here.
        std::vector<std::string> keys;
        keys.reserve(captured.nodes.size());
        for (const asset::AssetCatalogNode &node : captured.nodes)
        {
            if (node.stable_key.empty())
            {
                diagnostic_ = "Asset catalog refresh rejected: a node has no stable key";
                return false;
            }
            keys.push_back(node.stable_key);
        }
        std::sort(keys.begin(), keys.end());
        if (std::adjacent_find(keys.begin(), keys.end()) != keys.end())
        {
            diagnostic_ = "Asset catalog refresh rejected: duplicate stable key";
            return false;
        }

        snapshot_ = std::move(captured);
        has_snapshot_ = true;
        revision_ = snapshot_.revision;
        node_count_ = snapshot_.nodes.size();
        partial_ = snapshot_.status == asset::AssetCatalogSnapshotStatus::Partial;
        warning_count_ = 0;
        for (const asset::AssetCatalogDiagnostic &d : snapshot_.diagnostics)
        {
            if (d.severity == asset::AssetCatalogDiagnosticSeverity::Warning)
            {
                ++warning_count_;
            }
        }

        // A successful refresh clears the failure message, including a Partial one: a
        // Partial snapshot is a valid refresh that carries its own diagnostics.
        diagnostic_.clear();

        // Derived before the swap-in above could be observed, and rebuilt from the new
        // snapshot and the current query together.
        Rebuild();

        // Selection follows its stable key, and only clears when the key is really gone.
        if (!selected_key_.empty() && SelectedRow() == nullptr)
        {
            selected_key_.clear();
        }
        return true;
    }

    void AssetBrowserModel::SetSearch(std::string text)
    {
        if (query_.search == text)
        {
            return;
        }
        query_.search = std::move(text);
        Rebuild();
    }

    void AssetBrowserModel::SetLocation(AssetBrowserLocation location)
    {
        if (query_.location == location)
        {
            return;
        }
        query_.location = location;
        Rebuild();
    }

    void AssetBrowserModel::SetTypeFilter(std::vector<std::string> type_names)
    {
        std::sort(type_names.begin(), type_names.end());
        if (query_.included_type_names == type_names)
        {
            return;
        }
        query_.included_type_names = std::move(type_names);
        Rebuild();
    }

    void AssetBrowserModel::SetAvailabilityFilter(
        std::vector<asset::AssetCatalogAvailability> availability)
    {
        std::sort(availability.begin(), availability.end());
        if (query_.included_availability == availability)
        {
            return;
        }
        query_.included_availability = std::move(availability);
        Rebuild();
    }

    void AssetBrowserModel::SetLogicalPrefix(std::string prefix)
    {
        // Normalized to the folder name without its separator, so "material" and
        // "material/" select the same subtree: the folder list offers the bare name while a
        // caller typing a path naturally writes the slash.
        while (!prefix.empty() && prefix.back() == '/')
        {
            prefix.pop_back();
        }
        if (query_.logical_prefix == prefix)
        {
            return;
        }
        query_.logical_prefix = std::move(prefix);
        Rebuild();
    }

    void AssetBrowserModel::SetSort(AssetBrowserSortColumn column, bool ascending)
    {
        if (query_.sort_column == column && query_.ascending == ascending)
        {
            return;
        }
        query_.sort_column = column;
        query_.ascending = ascending;
        Rebuild();
    }

    void AssetBrowserModel::ToggleSort(AssetBrowserSortColumn column)
    {
        if (query_.sort_column == column)
        {
            SetSort(column, !query_.ascending);
            return;
        }
        SetSort(column, true);
    }

    void AssetBrowserModel::SetPresentation(AssetBrowserPresentation presentation) noexcept
    {
        // Presentation is not part of the projection: both modes draw the same rows.
        presentation_ = presentation;
    }

    void AssetBrowserModel::ResetProjection()
    {
        rows_.clear();
        folders_.clear();
        type_names_.clear();
    }

    void AssetBrowserModel::Rebuild()
    {
        ResetProjection();
        if (!has_snapshot_)
        {
            return;
        }

        const std::string search = FoldAscii(query_.search);
        // Terms are split on whitespace and ANDed, so more words narrow rather than widen.
        std::vector<std::string> terms;
        std::size_t start = 0;
        while (start < search.size())
        {
            while (start < search.size() &&
                   (search[start] == ' ' || search[start] == '\t'))
            {
                ++start;
            }
            const std::size_t end = search.find_first_of(" \t", start);
            if (end == start)
            {
                break;
            }
            terms.push_back(search.substr(start, end == std::string::npos ? end : end - start));
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }

        const std::string folded_prefix = FoldAscii(query_.logical_prefix);

        // Distinct type names come from every node, not from the surviving rows, so the
        // control that lists them cannot empty itself as it is used.
        for (const asset::AssetCatalogNode &node : snapshot_.nodes)
        {
            if (node.type_name.empty())
            {
                continue;
            }
            if (std::find(type_names_.begin(), type_names_.end(), node.type_name) ==
                type_names_.end())
            {
                type_names_.push_back(node.type_name);
            }
        }
        std::sort(type_names_.begin(), type_names_.end());

        // Folded once per row rather than once per comparison: a comparator that folded its
        // own keys would fold them O(n log n) times, and folding allocates.
        struct SortKeys
        {
            std::string name;
            std::string type;
            std::string path;
        };

        std::vector<std::string> prefixes;
        std::vector<SortKeys> sort_keys;    // parallel to rows_
        std::vector<std::size_t> row_bytes;  // parallel to rows_, for the size comparison
        for (const asset::AssetCatalogNode &node : snapshot_.nodes)
        {
            switch (query_.location)
            {
            case AssetBrowserLocation::ArchiveProducts:
                if (!IsArchiveProduct(node.availability))
                {
                    continue;
                }
                break;
            case AssetBrowserLocation::RuntimeOnly:
                if (node.availability != asset::AssetCatalogAvailability::RuntimeOnly)
                {
                    continue;
                }
                break;
            case AssetBrowserLocation::Missing:
                if (node.availability != asset::AssetCatalogAvailability::Missing)
                {
                    continue;
                }
                break;
            case AssetBrowserLocation::All:
                break;
            }

            if (!ContainsAnyType(query_.included_type_names, node.type_name) ||
                !ContainsAnyAvailability(query_.included_availability, node.availability))
            {
                continue;
            }

            if (!folded_prefix.empty())
            {
                // A component boundary, not a raw prefix: "material/bric" must not select
                // "material/brick", or the folder tree would file siblings under each
                // other. A folder is either the node's own path or a proper ancestor of it.
                const std::string folded_path = FoldAscii(node.logical_path);
                const bool is_self = folded_path == folded_prefix;
                const bool is_under =
                    folded_path.size() > folded_prefix.size() &&
                    folded_path.compare(0, folded_prefix.size(), folded_prefix) == 0 &&
                    folded_path[folded_prefix.size()] == '/';
                if (!is_self && !is_under)
                {
                    continue;
                }
            }

            if (!terms.empty())
            {
                // One haystack per node. Aliases and provenance paths are included because
                // a user searching for a file they imported knows that name, not the
                // display name the archive gave it.
                std::string haystack = FoldAscii(node.display_name);
                haystack += '\n';
                haystack += FoldAscii(node.type_name);
                haystack += '\n';
                haystack += FoldAscii(node.logical_path);
                haystack += '\n';
                haystack += FoldAscii(node.product_path);
                for (const std::string &alias : node.aliases)
                {
                    haystack += '\n';
                    haystack += FoldAscii(alias);
                }
                for (const asset::AssetCatalogProvenance &p : node.provenance)
                {
                    haystack += '\n';
                    haystack += FoldAscii(p.source_path);
                    haystack += '\n';
                    haystack += FoldAscii(p.source_display_name);
                    for (const std::string &dependency : p.source_dependency_paths)
                    {
                        haystack += '\n';
                        haystack += FoldAscii(dependency);
                    }
                }

                bool all_terms_found = true;
                for (const std::string &term : terms)
                {
                    if (haystack.find(term) == std::string::npos)
                    {
                        all_terms_found = false;
                        break;
                    }
                }
                if (!all_terms_found)
                {
                    continue;
                }
            }

            AssetBrowserRow row;
            row.stable_key = node.stable_key;
            row.display_name = node.display_name;
            row.type_name = node.type_name;
            row.logical_path = node.logical_path;
            row.product_path = node.product_path;
            row.state_label = std::string{StateLabel(node.availability)};
            row.availability = node.availability;
            row.kind = node.kind;
            // A zero byte size is "unknown" rather than "empty": the contract has no way to
            // say a product is genuinely zero bytes, and claiming 0 B for an unmeasured
            // product would be worse than admitting the gap.
            row.has_known_size = node.byte_size > 0;
            row.size_label = FormatAssetByteSize(node.byte_size > 0
                                                     ? std::optional<std::uint64_t>{node.byte_size}
                                                     : std::nullopt);
            row_bytes.push_back(node.byte_size);
            sort_keys.push_back(SortKeys{FoldAscii(row.display_name), FoldAscii(row.type_name),
                                         FoldAscii(row.logical_path)});

            // Folder counts come from the rows that survived filtering, which is what makes
            // them agree with what the user is looking at.
            if (!node.logical_path.empty())
            {
                CollectFolderPrefixes(node.logical_path, prefixes);
                for (const std::string &prefix : prefixes)
                {
                    const auto it = std::find_if(
                        folders_.begin(), folders_.end(),
                        [&prefix](const AssetBrowserFolder &folder)
                        { return folder.path == prefix; });
                    if (it == folders_.end())
                    {
                        folders_.push_back(AssetBrowserFolder{prefix, 1});
                    }
                    else
                    {
                        ++it->count;
                    }
                }
            }

            rows_.push_back(std::move(row));
        }

        // Stable and total: the chosen column first, then bytewise tie-breakers that never
        // depend on the direction, so equal rows still have exactly one order.
        const auto compare = [this, &sort_keys, &row_bytes](std::size_t lhs, std::size_t rhs)
        {
            const AssetBrowserRow &a = rows_[lhs];
            const AssetBrowserRow &b = rows_[rhs];
            const SortKeys &ka = sort_keys[lhs];
            const SortKeys &kb = sort_keys[rhs];

            if (query_.sort_column == AssetBrowserSortColumn::Size)
            {
                // Applies its own direction, so it must not be re-oriented below.
                const int size = CompareSize(a.has_known_size, b.has_known_size,
                                             row_bytes[lhs], row_bytes[rhs], query_.ascending);
                if (size != 0)
                {
                    return size < 0;
                }
            }
            else
            {
                int primary = 0;
                switch (query_.sort_column)
                {
                case AssetBrowserSortColumn::Name:
                    primary = CompareBytes(ka.name, kb.name);
                    break;
                case AssetBrowserSortColumn::Type:
                    primary = CompareBytes(ka.type, kb.type);
                    break;
                case AssetBrowserSortColumn::Availability:
                    primary = CompareBytes(a.state_label, b.state_label);
                    break;
                case AssetBrowserSortColumn::Path:
                    primary = CompareBytes(ka.path, kb.path);
                    break;
                case AssetBrowserSortColumn::Size:
                    break;  // handled above
                }
                if (primary != 0)
                {
                    return query_.ascending ? primary < 0 : primary > 0;
                }
            }

            if (const int name = CompareBytes(a.display_name, b.display_name); name != 0)
            {
                return name < 0;
            }
            if (const int type = CompareBytes(a.type_name, b.type_name); type != 0)
            {
                return type < 0;
            }
            return a.stable_key < b.stable_key;
        };

        std::vector<std::size_t> order(rows_.size());
        for (std::size_t i = 0; i < order.size(); ++i)
        {
            order[i] = i;
        }
        std::stable_sort(order.begin(), order.end(), compare);

        std::vector<AssetBrowserRow> sorted;
        sorted.reserve(rows_.size());
        for (const std::size_t index : order)
        {
            sorted.push_back(std::move(rows_[index]));
        }
        rows_ = std::move(sorted);

        std::sort(folders_.begin(), folders_.end(),
                  [](const AssetBrowserFolder &a, const AssetBrowserFolder &b)
                  { return a.path < b.path; });
    }

    void AssetBrowserModel::Select(std::string_view stable_key)
    {
        if (stable_key.empty())
        {
            return;
        }
        selected_key_ = std::string{stable_key};
    }

    void AssetBrowserModel::ClearSelection() noexcept
    {
        selected_key_.clear();
    }

    const AssetBrowserRow *AssetBrowserModel::SelectedRow() const noexcept
    {
        if (selected_key_.empty())
        {
            return nullptr;
        }
        const auto it = std::find_if(rows_.begin(), rows_.end(),
                                     [this](const AssetBrowserRow &row)
                                     { return row.stable_key == selected_key_; });
        return it == rows_.end() ? nullptr : &*it;
    }

    AssetBrowserDetails AssetBrowserModel::SelectedDetails() const
    {
        AssetBrowserDetails details;
        if (selected_key_.empty())
        {
            return details;
        }
        const auto it = std::find_if(
            snapshot_.nodes.begin(), snapshot_.nodes.end(),
            [this](const asset::AssetCatalogNode &node)
            { return node.stable_key == selected_key_; });
        if (it == snapshot_.nodes.end())
        {
            return details;
        }

        details.aliases = it->aliases;
        details.dependency_coverage_complete =
            it->dependency_coverage ==
            asset::AssetCatalogDependencyCoverage::Complete;
        // Readable source paths only. Content hashes and packed ids are deliberately absent:
        // they are not something a reader can act on, and the plan keeps them out of the
        // primary label.
        for (const asset::AssetCatalogProvenance &provenance : it->provenance)
        {
            if (!provenance.source_path.empty())
            {
                details.source_paths.push_back(provenance.source_path);
            }
        }
        return details;
    }

    std::optional<std::size_t> AssetBrowserModel::MoveSelection(int delta)
    {
        if (rows_.empty())
        {
            return std::nullopt;
        }

        std::size_t index = 0;
        if (const AssetBrowserRow *const current = SelectedRow(); current != nullptr)
        {
            index = static_cast<std::size_t>(current - rows_.data());
            const int next = static_cast<int>(index) + delta;
            index = next < 0 ? 0
                             : std::min(static_cast<std::size_t>(next), rows_.size() - 1);
        }
        selected_key_ = rows_[index].stable_key;
        return index;
    }
}
