#ifndef KPENGINE_EDITOR_ASSET_REFERENCE_VIEW_MODEL_H
#define KPENGINE_EDITOR_ASSET_REFERENCE_VIEW_MODEL_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "editor/asset/asset_browser_model.h"

namespace kpengine::editor
{
    enum class AssetReferenceDirection : std::uint8_t
    {
        Dependencies,
        Referencers,
    };

    enum class AssetReferencePresentation : std::uint8_t
    {
        Tree,
        Text,
    };

    struct AssetReferenceTraversalLimits
    {
        std::uint32_t max_depth{32};
        std::uint32_t max_rows{4096};
    };

    enum class AssetReferenceRowKind : std::uint8_t
    {
        Root,
        Edge,
        SharedLeaf,
        CycleLeaf,
        MissingLeaf,
        UnknownCoverageLeaf,
        Truncation,
    };

    struct AssetReferenceRow
    {
        std::string occurrence_key;
        std::string stable_key;
        std::string display_name;
        std::string type_name;
        std::string state_label;
        std::string logical_path;
        std::string product_path;
        std::string relation_token;
        std::string relation_label;
        std::string annotation;
        std::uint32_t depth{};
        AssetReferenceRowKind kind{AssetReferenceRowKind::Root};
        bool has_children{false};
        bool expanded{false};
    };

    class AssetReferenceViewModel final
    {
    public:
        explicit AssetReferenceViewModel(AssetBrowserModel &browser) noexcept;

        // The browser model remains the single snapshot owner. This call stores only
        // an opaque key and rebuilds Editor-owned indexes/projections.
        void SetRoot(std::string stable_key);
        const std::string &RootKey() const noexcept { return root_key_; }
        bool HasResolvedRoot() const noexcept { return root_resolved_; }
        const std::string &RootDiagnostic() const noexcept { return root_diagnostic_; }

        void SetDirection(AssetReferenceDirection direction);
        AssetReferenceDirection Direction() const noexcept { return direction_; }
        void SetPresentation(AssetReferencePresentation presentation) noexcept
        {
            presentation_ = presentation;
        }
        AssetReferencePresentation Presentation() const noexcept { return presentation_; }

        void SetLimits(AssetReferenceTraversalLimits limits);
        AssetReferenceTraversalLimits Limits() const noexcept { return limits_; }

        // Synchronizes only when the browser promotes a different snapshot revision.
        // It never captures a snapshot itself.
        void Sync();
        void Reset() noexcept;

        void ToggleExpanded(std::string_view occurrence_key);
        void ExpandAll();
        void CollapseAll();

        void SelectOccurrence(std::string_view occurrence_key);
        void ClearSelection() noexcept { selected_occurrence_.clear(); }
        const std::string &SelectedOccurrence() const noexcept { return selected_occurrence_; }
        const AssetReferenceRow *SelectedRow() const noexcept;

        const std::vector<AssetReferenceRow> &Rows() const noexcept { return rows_; }
        const std::string &ExportedText() const
        {
            EnsureTextExport();
            return exported_text_;
        }
        bool HasSnapshot() const noexcept { return indexed_revision_.has_value(); }

        static std::string RelationToken(asset::AssetCatalogRelation relation);
        static std::string EscapeText(std::string_view text);

    private:
        struct NodeRecord
        {
            std::string stable_key;
            std::string display_name;
            std::string type_name;
            std::string logical_path;
            std::string product_path;
            std::string state_label;
            asset::AssetCatalogNodeKind kind{asset::AssetCatalogNodeKind::Asset};
            asset::AssetCatalogAvailability availability{
                asset::AssetCatalogAvailability::RuntimeOnly};
            asset::AssetCatalogDependencyCoverage dependency_coverage{
                asset::AssetCatalogDependencyCoverage::Unknown};
        };

        struct EdgeRecord
        {
            std::string from_key;
            std::string to_key;
            asset::AssetCatalogRelation relation{asset::AssetCatalogRelation::Dependency};
            std::uint32_t ordinal{};
            std::string label;
        };

        using Adjacency = std::unordered_map<std::string, std::vector<EdgeRecord>>;

        void RebuildIndex();
        void RebuildProjection();
        std::vector<AssetReferenceRow> BuildRows(bool all_expanded) const;
        void EnsureTextExport() const;
        void BuildTextExport() const;
        bool IsExpanded(std::string_view occurrence_key, bool all_expanded) const;
        std::unordered_set<std::string> &ExpansionSet() noexcept;
        const std::unordered_set<std::string> &ExpansionSet() const noexcept;
        std::string RootOccurrenceKey() const;

        AssetBrowserModel &browser_;
        std::optional<std::uint64_t> indexed_revision_;
        std::unordered_map<std::string, NodeRecord> nodes_;
        Adjacency forward_;
        Adjacency reverse_;
        std::string root_key_;
        std::string root_diagnostic_;
        bool root_resolved_{false};
        AssetReferenceDirection direction_{AssetReferenceDirection::Dependencies};
        AssetReferencePresentation presentation_{AssetReferencePresentation::Tree};
        AssetReferenceTraversalLimits limits_{};
        std::unordered_set<std::string> dependency_expanded_;
        std::unordered_set<std::string> referencer_expanded_;
        std::vector<AssetReferenceRow> rows_;
        mutable std::string exported_text_;
        mutable bool exported_text_dirty_{true};
        std::string selected_occurrence_;
    };
}

#endif // KPENGINE_EDITOR_ASSET_REFERENCE_VIEW_MODEL_H
