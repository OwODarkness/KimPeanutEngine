#include "editor/asset/asset_reference_view_model.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <utility>

namespace kpengine::editor
{
    namespace
    {
        constexpr std::uint32_t kMaximumDepth{32};
        constexpr std::uint32_t kMaximumRows{4096};

        void AppendEscaped(std::string &output, std::string_view text)
        {
            for (const char character : text)
            {
                switch (character)
                {
                case '\\':
                    output += "\\\\";
                    break;
                case '\n':
                    output += "\\n";
                    break;
                case '\r':
                    output += "\\r";
                    break;
                case '\t':
                    output += "\\t";
                    break;
                case '{':
                    output += "\\{";
                    break;
                case '}':
                    output += "\\}";
                    break;
                default:
                    output.push_back(character);
                    break;
                }
            }
        }

        std::string DirectionLabel(AssetReferenceDirection direction)
        {
            return direction == AssetReferenceDirection::Dependencies ? "Dependencies"
                                                                        : "Referencers";
        }

        std::string DirectionToken(AssetReferenceDirection direction)
        {
            return direction == AssetReferenceDirection::Dependencies ? "dependencies"
                                                                        : "referencers";
        }

    }

    AssetReferenceViewModel::AssetReferenceViewModel(AssetBrowserModel &browser) noexcept
        : browser_(browser)
    {
    }

    void AssetReferenceViewModel::SetRoot(std::string stable_key)
    {
        if (root_key_ == stable_key)
        {
            Sync();
            return;
        }

        root_key_ = std::move(stable_key);
        dependency_expanded_.clear();
        referencer_expanded_.clear();
        selected_occurrence_.clear();
        Sync();
        if (!root_key_.empty())
        {
            ExpansionSet().insert(RootOccurrenceKey());
            RebuildProjection();
        }
    }

    void AssetReferenceViewModel::SetDirection(AssetReferenceDirection direction)
    {
        if (direction_ == direction)
        {
            return;
        }
        direction_ = direction;
        selected_occurrence_.clear();
        if (!root_key_.empty())
        {
            ExpansionSet().insert(RootOccurrenceKey());
        }
        RebuildProjection();
    }

    void AssetReferenceViewModel::SetLimits(AssetReferenceTraversalLimits limits)
    {
        limits.max_depth = std::min(limits.max_depth, kMaximumDepth);
        limits.max_rows = std::min(std::max(limits.max_rows, 1u), kMaximumRows);
        if (limits_.max_depth == limits.max_depth && limits_.max_rows == limits.max_rows)
        {
            return;
        }
        limits_ = limits;
        RebuildProjection();
    }

    void AssetReferenceViewModel::Sync()
    {
        const asset::AssetCatalogSnapshot *const snapshot = browser_.Snapshot();
        const std::optional<std::uint64_t> revision =
            snapshot != nullptr ? std::optional<std::uint64_t>{browser_.Revision()} : std::nullopt;
        if (revision == indexed_revision_)
        {
            return;
        }
        RebuildIndex();
    }

    void AssetReferenceViewModel::Reset() noexcept
    {
        indexed_revision_.reset();
        nodes_.clear();
        forward_.clear();
        reverse_.clear();
        root_key_.clear();
        root_diagnostic_.clear();
        root_resolved_ = false;
        dependency_expanded_.clear();
        referencer_expanded_.clear();
        rows_.clear();
        exported_text_.clear();
        exported_text_dirty_ = false;
        selected_occurrence_.clear();
    }

    void AssetReferenceViewModel::RebuildIndex()
    {
        nodes_.clear();
        forward_.clear();
        reverse_.clear();

        const asset::AssetCatalogSnapshot *const snapshot = browser_.Snapshot();
        if (snapshot == nullptr)
        {
            indexed_revision_.reset();
            root_resolved_ = false;
            root_diagnostic_ = "Asset catalog unavailable";
            rows_.clear();
            exported_text_.clear();
            exported_text_dirty_ = false;
            return;
        }

        indexed_revision_ = browser_.Revision();
        nodes_.reserve(snapshot->nodes.size());
        std::vector<std::string> dense_keys(snapshot->nodes.size());
        for (const asset::AssetCatalogNode &node : snapshot->nodes)
        {
            if (node.id.value >= dense_keys.size() || node.stable_key.empty())
            {
                continue;
            }
            NodeRecord copy;
            copy.stable_key = node.stable_key;
            copy.display_name = node.display_name;
            copy.type_name = node.type_name;
            copy.logical_path = node.logical_path;
            copy.product_path = node.product_path;
            copy.state_label = std::string{AssetBrowserModel::StateLabel(node.availability)};
            copy.kind = node.kind;
            copy.availability = node.availability;
            copy.dependency_coverage = node.dependency_coverage;
            dense_keys[node.id.value] = copy.stable_key;
            nodes_.insert_or_assign(copy.stable_key, std::move(copy));
        }

        for (const asset::AssetCatalogEdge &edge : snapshot->edges)
        {
            if (edge.from.value >= dense_keys.size() || edge.to.value >= dense_keys.size())
            {
                continue;
            }
            const std::string &from_key = dense_keys[edge.from.value];
            const std::string &to_key = dense_keys[edge.to.value];
            if (from_key.empty() || to_key.empty() || nodes_.find(from_key) == nodes_.end() ||
                nodes_.find(to_key) == nodes_.end())
            {
                continue;
            }

            EdgeRecord record;
            record.from_key = from_key;
            record.to_key = to_key;
            record.relation = edge.relation;
            record.ordinal = edge.ordinal;
            record.label = edge.label;
            forward_[from_key].push_back(record);
            EdgeRecord reverse_record = record;
            // In the reverse projection the original source is the displayed target.
            // Keep it in from_key as well so the deterministic reverse sort remains
            // source-key first without retaining a second graph representation.
            reverse_record.to_key = from_key;
            reverse_record.from_key = from_key;
            reverse_[to_key].push_back(std::move(reverse_record));
        }

        for (auto &[key, edges] : forward_)
        {
            std::sort(edges.begin(), edges.end(),
                      [](const EdgeRecord &lhs, const EdgeRecord &rhs)
                      {
                          if (lhs.relation != rhs.relation)
                          {
                              return static_cast<unsigned int>(lhs.relation) <
                                     static_cast<unsigned int>(rhs.relation);
                          }
                          if (lhs.ordinal != rhs.ordinal)
                          {
                              return lhs.ordinal < rhs.ordinal;
                          }
                          return lhs.to_key < rhs.to_key;
                      });
        }
        for (auto &[key, edges] : reverse_)
        {
            std::sort(edges.begin(), edges.end(),
                      [](const EdgeRecord &lhs, const EdgeRecord &rhs)
                      {
                          if (lhs.from_key != rhs.from_key)
                          {
                              return lhs.from_key < rhs.from_key;
                          }
                          if (lhs.relation != rhs.relation)
                          {
                              return static_cast<unsigned int>(lhs.relation) <
                                     static_cast<unsigned int>(rhs.relation);
                          }
                          return lhs.ordinal < rhs.ordinal;
                      });
        }

        const auto root = nodes_.find(root_key_);
        root_resolved_ = !root_key_.empty() && root != nodes_.end();
        root_diagnostic_ = root_resolved_
                               ? std::string{}
                               : (root_key_.empty()
                                      ? std::string{"Select an asset to inspect references"}
                                      : "Root is not present in catalog revision " +
                                            std::to_string(browser_.Revision()));
        RebuildProjection();
    }

    void AssetReferenceViewModel::RebuildProjection()
    {
        rows_ = BuildRows(false);
        exported_text_dirty_ = true;
        if (!selected_occurrence_.empty() && SelectedRow() == nullptr)
        {
            selected_occurrence_.clear();
        }
    }

    bool AssetReferenceViewModel::IsExpanded(std::string_view occurrence_key,
                                             bool all_expanded) const
    {
        return all_expanded || ExpansionSet().find(std::string{occurrence_key}) !=
                                   ExpansionSet().end();
    }

    std::vector<AssetReferenceRow> AssetReferenceViewModel::BuildRows(bool all_expanded) const
    {
        std::vector<AssetReferenceRow> result;
        if (root_key_.empty())
        {
            return result;
        }

        const auto root = nodes_.find(root_key_);
        if (root == nodes_.end())
        {
            AssetReferenceRow unavailable;
            unavailable.occurrence_key = RootOccurrenceKey();
            unavailable.display_name = "Root unavailable";
            unavailable.state_label = "Missing";
            unavailable.annotation = root_diagnostic_;
            result.push_back(std::move(unavailable));
            return result;
        }

        const Adjacency &adjacency = direction_ == AssetReferenceDirection::Dependencies
                                         ? forward_
                                         : reverse_;
        const AssetReferenceDirection direction = direction_;
        const auto has_children = [&adjacency, direction](const NodeRecord &node,
                                                          std::string_view key)
        {
            const auto it = adjacency.find(std::string{key});
            return (it != adjacency.end() && !it->second.empty()) ||
                   (direction == AssetReferenceDirection::Dependencies &&
                    node.dependency_coverage == asset::AssetCatalogDependencyCoverage::Unknown);
        };
        const auto make_occurrence_key = [](std::string_view parent, const EdgeRecord &edge)
        {
            std::string key{parent};
            key += "|r=";
            key += std::to_string(static_cast<unsigned int>(edge.relation));
            key += "|o=";
            key += std::to_string(edge.ordinal);
            key += "|k=";
            key += std::to_string(edge.to_key.size());
            key += ":";
            key += edge.to_key;
            return key;
        };
        const std::string root_occurrence = RootOccurrenceKey();
        const bool root_has_children = has_children(root->second, root_key_);
        AssetReferenceRow root_row;
        root_row.occurrence_key = root_occurrence;
        root_row.stable_key = root->second.stable_key;
        root_row.display_name = root->second.display_name;
        root_row.type_name = root->second.type_name;
        root_row.state_label = root->second.state_label;
        root_row.logical_path = root->second.logical_path;
        root_row.product_path = root->second.product_path;
        root_row.kind = AssetReferenceRowKind::Root;
        root_row.has_children = root_has_children;
        root_row.expanded = root_has_children && IsExpanded(root_occurrence, all_expanded);
        result.push_back(std::move(root_row));

        std::unordered_set<std::string> ancestors;
        ancestors.insert(root_key_);
        std::unordered_map<std::string, std::uint32_t> ancestor_depths;
        ancestor_depths.emplace(root_key_, 0u);
        std::unordered_map<std::string, std::size_t> first_rows;
        first_rows.emplace(root_key_, 1u);

        const auto append_truncation = [&](std::string_view parent_occurrence,
                                           std::uint32_t parent_depth,
                                           std::string_view reason)
        {
            if (result.size() >= limits_.max_rows)
            {
                return;
            }
            AssetReferenceRow row;
            row.occurrence_key = std::string{parent_occurrence} + "|truncation";
            row.display_name = "Traversal limit";
            row.type_name = "Limit";
            row.state_label = "Truncated";
            row.annotation = std::string{reason};
            row.depth = parent_depth + 1;
            row.kind = AssetReferenceRowKind::Truncation;
            result.push_back(std::move(row));
        };

        const auto append_unknown_coverage = [&](std::string_view parent_occurrence,
                                                 std::uint32_t parent_depth)
        {
            if (result.size() >= limits_.max_rows)
            {
                return;
            }
            AssetReferenceRow row;
            row.occurrence_key = std::string{parent_occurrence} + "|unknown-coverage";
            row.display_name = "Additional dependencies";
            row.type_name = "Unknown";
            row.state_label = "Unknown";
            row.annotation = "additional archive dependencies unknown";
            row.depth = parent_depth + 1;
            row.kind = AssetReferenceRowKind::UnknownCoverageLeaf;
            result.push_back(std::move(row));
        };

        const auto append_children = [&](auto &&self, const NodeRecord &parent,
                                         std::string_view parent_occurrence,
                                         std::uint32_t parent_depth) -> void
        {
            const auto edge_it = adjacency.find(parent.stable_key);
            const bool has_edges = edge_it != adjacency.end() && !edge_it->second.empty();
            const bool unknown_coverage =
                direction == AssetReferenceDirection::Dependencies &&
                parent.dependency_coverage == asset::AssetCatalogDependencyCoverage::Unknown;
            if (!has_edges && !unknown_coverage)
            {
                return;
            }
            if (parent_depth >= limits_.max_depth)
            {
                append_truncation(parent_occurrence, parent_depth, "depth limit");
                return;
            }

            if (has_edges)
            {
                for (const EdgeRecord &edge : edge_it->second)
                {
                    if (result.size() >= limits_.max_rows)
                    {
                        append_truncation(parent_occurrence, parent_depth, "row limit");
                        return;
                    }

                    const auto target = nodes_.find(edge.to_key);
                    if (target == nodes_.end())
                    {
                        continue;
                    }
                    const std::string occurrence = make_occurrence_key(parent_occurrence, edge);
                    const bool cycle = ancestors.find(target->first) != ancestors.end();
                    const auto first = first_rows.find(target->first);

                    AssetReferenceRow row;
                    row.occurrence_key = occurrence;
                    row.stable_key = target->second.stable_key;
                    row.display_name = target->second.display_name;
                    row.type_name = target->second.type_name;
                    row.state_label = target->second.state_label;
                    row.logical_path = target->second.logical_path;
                    row.product_path = target->second.product_path;
                    row.relation_token = RelationToken(edge.relation);
                    row.relation_label = edge.label;
                    row.depth = parent_depth + 1;

                    if (cycle)
                    {
                        row.kind = AssetReferenceRowKind::CycleLeaf;
                        row.annotation = "cycle to depth " +
                                          std::to_string(ancestor_depths.at(target->first));
                    }
                    else if (first != first_rows.end())
                    {
                        row.kind = AssetReferenceRowKind::SharedLeaf;
                        row.annotation = "shared; first row " + std::to_string(first->second);
                    }
                    else if (target->second.kind == asset::AssetCatalogNodeKind::MissingReference ||
                             target->second.availability == asset::AssetCatalogAvailability::Missing)
                    {
                        row.kind = AssetReferenceRowKind::MissingLeaf;
                        row.annotation = "unresolved reference";
                    }
                    else
                    {
                        row.kind = AssetReferenceRowKind::Edge;
                        row.has_children = has_children(target->second, target->first);
                        row.expanded = row.has_children &&
                                       IsExpanded(occurrence, all_expanded);
                    }
                    result.push_back(std::move(row));

                    if (result.back().kind == AssetReferenceRowKind::Edge)
                    {
                        const bool can_expand = result.back().has_children &&
                                                result.back().expanded;
                        if (!result.back().has_children || can_expand)
                        {
                            first_rows.emplace(target->first, result.size());
                        }
                        if (can_expand)
                        {
                            ancestors.insert(target->first);
                            ancestor_depths.emplace(target->first, parent_depth + 1);
                            self(self, target->second, occurrence, parent_depth + 1);
                            ancestor_depths.erase(target->first);
                            ancestors.erase(target->first);
                        }
                    }
                }
            }
            if (unknown_coverage)
            {
                append_unknown_coverage(parent_occurrence, parent_depth);
            }
        };

        if (root_row.expanded)
        {
            append_children(append_children, root->second, root_occurrence, 0);
        }
        return result;
    }

    void AssetReferenceViewModel::EnsureTextExport() const
    {
        if (!exported_text_dirty_)
        {
            return;
        }
        BuildTextExport();
        exported_text_dirty_ = false;
    }

    void AssetReferenceViewModel::BuildTextExport() const
    {
        const std::vector<AssetReferenceRow> all_rows = BuildRows(true);
        std::string output;
        output.reserve(all_rows.size() * 64u);
        output += DirectionLabel(direction_);
        output += ": ";
        if (!root_resolved_)
        {
            output += "<missing root>";
            if (!root_diagnostic_.empty())
            {
                output += " {";
                AppendEscaped(output, root_diagnostic_);
                output += "}";
            }
            output.push_back('\n');
            exported_text_ = std::move(output);
            return;
        }

        for (std::size_t index = 0; index < all_rows.size(); ++index)
        {
            const AssetReferenceRow &row = all_rows[index];
            if (index == 0)
            {
                AppendEscaped(output, row.type_name);
                output.push_back(' ');
                AppendEscaped(output, row.display_name);
                output += " [";
                AppendEscaped(output, row.state_label);
                output += "]\n";
                continue;
            }

            output.append(static_cast<std::size_t>(row.depth) * 2u, ' ');
            if (row.kind == AssetReferenceRowKind::Truncation)
            {
                output += "... {truncated: ";
                AppendEscaped(output, row.annotation);
                output += "}\n";
                continue;
            }
            if (row.kind == AssetReferenceRowKind::UnknownCoverageLeaf)
            {
                output += "... {";
                AppendEscaped(output, row.annotation);
                output += "}\n";
                continue;
            }

            output += row.relation_token.empty() ? "dependency" : row.relation_token;
            output += " -> ";
            AppendEscaped(output, row.type_name.empty() ? "<missing>" : row.type_name);
            output.push_back(' ');
            AppendEscaped(output, row.display_name);
            output += " [";
            AppendEscaped(output, row.state_label);
            output += "]";
            if (!row.logical_path.empty() && row.logical_path != row.display_name)
            {
                output += " (";
                AppendEscaped(output, row.logical_path);
                output.push_back(')');
            }
            if (!row.annotation.empty())
            {
                output += " {";
                AppendEscaped(output, row.annotation);
                output += "}";
            }
            output.push_back('\n');
        }
        exported_text_ = std::move(output);
    }

    void AssetReferenceViewModel::ToggleExpanded(std::string_view occurrence_key)
    {
        if (occurrence_key.empty())
        {
            return;
        }
        std::unordered_set<std::string> &set = ExpansionSet();
        const auto it = set.find(std::string{occurrence_key});
        if (it == set.end())
        {
            set.emplace(occurrence_key);
        }
        else
        {
            set.erase(it);
        }
        RebuildProjection();
    }

    void AssetReferenceViewModel::ExpandAll()
    {
        if (root_key_.empty() || !root_resolved_)
        {
            return;
        }
        const std::vector<AssetReferenceRow> all_rows = BuildRows(true);
        std::unordered_set<std::string> &set = ExpansionSet();
        for (const AssetReferenceRow &row : all_rows)
        {
            if (row.has_children)
            {
                set.insert(row.occurrence_key);
            }
        }
        RebuildProjection();
    }

    void AssetReferenceViewModel::CollapseAll()
    {
        std::unordered_set<std::string> &set = ExpansionSet();
        set.clear();
        if (!root_key_.empty())
        {
            set.insert(RootOccurrenceKey());
        }
        RebuildProjection();
    }

    void AssetReferenceViewModel::SelectOccurrence(std::string_view occurrence_key)
    {
        for (const AssetReferenceRow &row : rows_)
        {
            if (row.occurrence_key == occurrence_key)
            {
                selected_occurrence_ = std::string{occurrence_key};
                return;
            }
        }
        selected_occurrence_.clear();
    }

    const AssetReferenceRow *AssetReferenceViewModel::SelectedRow() const noexcept
    {
        if (selected_occurrence_.empty())
        {
            return nullptr;
        }
        const auto it = std::find_if(rows_.begin(), rows_.end(), [this](const AssetReferenceRow &row)
                                     { return row.occurrence_key == selected_occurrence_; });
        return it == rows_.end() ? nullptr : &*it;
    }

    std::string AssetReferenceViewModel::RelationToken(asset::AssetCatalogRelation relation)
    {
        return relation == asset::AssetCatalogRelation::OwnedChild ? "owned-child"
                                                                     : "dependency";
    }

    std::string AssetReferenceViewModel::EscapeText(std::string_view text)
    {
        std::string result;
        result.reserve(text.size());
        AppendEscaped(result, text);
        return result;
    }

    std::unordered_set<std::string> &AssetReferenceViewModel::ExpansionSet() noexcept
    {
        return direction_ == AssetReferenceDirection::Dependencies ? dependency_expanded_
                                                                    : referencer_expanded_;
    }

    const std::unordered_set<std::string> &AssetReferenceViewModel::ExpansionSet() const noexcept
    {
        return direction_ == AssetReferenceDirection::Dependencies ? dependency_expanded_
                                                                    : referencer_expanded_;
    }

    std::string AssetReferenceViewModel::RootOccurrenceKey() const
    {
        return "root/" + DirectionToken(direction_);
    }
}
