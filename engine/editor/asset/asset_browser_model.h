#ifndef KPENGINE_EDITOR_ASSET_BROWSER_MODEL_H
#define KPENGINE_EDITOR_ASSET_BROWSER_MODEL_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asset/asset_catalog.h"

// This header must stay ImGui-free and Asset-library-free. It includes the frozen catalog
// contract for its value types only: the browsing rules — filtering, ordering, selection,
// and the refresh transaction — live here so they are testable without a frame, and the
// component is a thin translation of what this model already decided.
//
// It deliberately does NOT call ValidateAssetCatalogSnapshot. Asset canonicalizes and
// validates every snapshot before publishing it (AB1.1), so re-running that check here
// would widen the Editor's link to AssetRuntime and duplicate a guarantee Asset already
// makes. What the Editor adds instead is a check of the one property IT depends on: that
// stable keys are non-empty and unique, because a stable key is the selection identity.

namespace kpengine::editor
{
    enum class AssetBrowserPresentation : std::uint8_t
    {
        Table,
        CompactTiles,
    };

    enum class AssetBrowserSortColumn : std::uint8_t
    {
        Name,
        Type,
        Availability,
        Size,
        Path,
    };

    // The catalog is a projection, not a filesystem. Each location maps onto the
    // availability the snapshot already carries.
    enum class AssetBrowserLocation : std::uint8_t
    {
        All,
        ArchiveProducts,  // ArchiveOnly + LoadedArchiveProduct
        RuntimeOnly,
        Missing,
    };

    struct AssetBrowserQuery
    {
        std::string search;
        AssetBrowserLocation location{AssetBrowserLocation::All};
        std::vector<std::string> included_type_names;
        std::vector<asset::AssetCatalogAvailability> included_availability;
        AssetBrowserSortColumn sort_column{AssetBrowserSortColumn::Name};
        bool ascending{true};
        // Logical-path prefix navigation. Not in the stage plan's struct, and added because
        // its navigation rules require selecting a folder: a prefix is the only form of
        // that selection that can be expressed as data rather than as view state.
        std::string logical_prefix;
    };

    // One row, flattened. The view never receives a catalog node, a mutable reference, or
    // anything it could use to reach past the snapshot.
    struct AssetBrowserRow
    {
        std::string stable_key;
        std::string display_name;
        std::string type_name;
        std::string logical_path;
        std::string product_path;
        std::string state_label;  // exactly Loaded / Archive / Runtime / Missing
        std::string size_label;   // binary units, or an em dash when unavailable
        asset::AssetCatalogAvailability availability{
            asset::AssetCatalogAvailability::RuntimeOnly};
        asset::AssetCatalogNodeKind kind{asset::AssetCatalogNodeKind::Asset};
        bool has_known_size{false};
    };

    struct AssetBrowserFolder
    {
        std::string path;
        std::size_t count{};
    };

    // Everything the details pane shows beyond what a row already carries. Separate from
    // AssetBrowserRow so a hundred thousand rows do not each hold a provenance list they
    // will never display.
    struct AssetBrowserDetails
    {
        std::vector<std::string> aliases;
        std::vector<std::string> source_paths;
        bool dependency_coverage_complete{false};
    };

    // Binary units, and an em dash when a size is unknown. Free rather than a member so the
    // component and the tests format a size the same way.
    std::string FormatAssetByteSize(const std::optional<std::uint64_t> &bytes);

    // ASCII-only case folding, locale-independent: A-Z fold, every other byte is unchanged,
    // so UTF-8 text matches bytewise rather than through a locale.
    std::string FoldAscii(std::string_view text);

    class AssetBrowserModel final
    {
    public:
        // Borrowed, not owned. Null is supported and produces the unavailable state rather
        // than a crash: lifecycle tests and a Runtime without scene services both pass null.
        void SetSource(asset::IAssetCatalogSnapshotSource *source) noexcept;

        bool IsSourceAvailable() const noexcept { return source_ != nullptr; }
        bool HasSnapshot() const noexcept { return has_snapshot_; }
        std::uint64_t Revision() const noexcept { return revision_; }
        std::size_t NodeCount() const noexcept { return node_count_; }
        std::size_t WarningCount() const noexcept { return warning_count_; }
        bool SnapshotWasPartial() const noexcept { return partial_; }

        // The Editor's own message about the last refresh, or about the source being
        // absent. Empty when the last refresh succeeded. Never Asset's diagnostics, which
        // the provider already folded into the snapshot.
        const std::string &Diagnostic() const noexcept { return diagnostic_; }

        // The refresh transaction. Captures, checks the result, derives every index into
        // locals, and only then replaces what it had — so a failed refresh leaves the last
        // valid snapshot and the selection untouched. Synchronous, and never called from a
        // frame: the caller decides when.
        bool Refresh();

        const AssetBrowserQuery &Query() const noexcept { return query_; }
        // Each setter rebuilds only when the value actually changed, so an unchanged frame
        // costs nothing and the plan's "changing query rebuilds only Editor indexes" holds
        // without the component having to remember to ask.
        void SetSearch(std::string text);
        void SetLocation(AssetBrowserLocation location);
        void SetTypeFilter(std::vector<std::string> type_names);
        void SetAvailabilityFilter(std::vector<asset::AssetCatalogAvailability> availability);
        void SetLogicalPrefix(std::string prefix);
        void SetSort(AssetBrowserSortColumn column, bool ascending);
        // Clicking a column header: same column flips direction, a new column starts
        // ascending. Comparison stays bytewise and deterministic either way.
        void ToggleSort(AssetBrowserSortColumn column);

        void SetPresentation(AssetBrowserPresentation presentation) noexcept;
        AssetBrowserPresentation Presentation() const noexcept { return presentation_; }

        // The projection of the current snapshot through the current query. Both are
        // rebuilt together, so a reader can never see one derived from the other's old
        // value.
        const std::vector<AssetBrowserRow> &Rows() const noexcept { return rows_; }
        const std::vector<AssetBrowserFolder> &Folders() const noexcept { return folders_; }
        // Distinct type names in the whole snapshot, sorted bytewise. From the snapshot and
        // not from the rows, so the filter control cannot disappear as it is applied.
        const std::vector<std::string> &TypeNames() const noexcept { return type_names_; }

        void Select(std::string_view stable_key);
        void ClearSelection() noexcept;
        const std::string &SelectedKey() const noexcept { return selected_key_; }
        const AssetBrowserRow *SelectedRow() const noexcept;
        // Empty when nothing is selected or the selected key is no longer in the snapshot.
        AssetBrowserDetails SelectedDetails() const;
        // Keyboard navigation within the visible projection. Returns the new index, or
        // nullopt when there is nothing to select.
        std::optional<std::size_t> MoveSelection(int delta);

        static std::string_view StateLabel(asset::AssetCatalogAvailability availability) noexcept;
        static bool IsArchiveProduct(asset::AssetCatalogAvailability availability) noexcept;

    private:
        void Rebuild();
        void ResetProjection();

        asset::IAssetCatalogSnapshotSource *source_ = nullptr;
        asset::AssetCatalogSnapshot snapshot_;
        bool has_snapshot_ = false;
        std::uint64_t revision_ = 0;
        std::size_t node_count_ = 0;
        std::size_t warning_count_ = 0;
        bool partial_ = false;
        std::string diagnostic_;

        AssetBrowserQuery query_;
        AssetBrowserPresentation presentation_{AssetBrowserPresentation::CompactTiles};
        std::vector<AssetBrowserRow> rows_;
        std::vector<AssetBrowserFolder> folders_;
        std::vector<std::string> type_names_;
        std::string selected_key_;
    };
}

#endif // KPENGINE_EDITOR_ASSET_BROWSER_MODEL_H
