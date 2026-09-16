#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "editor/asset/asset_reference_view_model.h"

namespace
{
    using kpengine::asset::AssetCatalogAvailability;
    using kpengine::asset::AssetCatalogDependencyCoverage;
    using kpengine::asset::AssetCatalogEdge;
    using kpengine::asset::AssetCatalogNode;
    using kpengine::asset::AssetCatalogNodeKind;
    using kpengine::asset::AssetCatalogRelation;
    using kpengine::asset::AssetCatalogSnapshot;
    using kpengine::asset::IAssetCatalogSnapshotSource;
    using kpengine::editor::AssetBrowserModel;
    using kpengine::editor::AssetReferenceDirection;
    using kpengine::editor::AssetReferencePresentation;
    using kpengine::editor::AssetReferenceRowKind;
    using kpengine::editor::AssetReferenceViewModel;

    class FakeSource final : public IAssetCatalogSnapshotSource
    {
    public:
        AssetCatalogSnapshot snapshot;
        int capture_count{};

        AssetCatalogSnapshot CaptureAssetCatalog() override
        {
            ++capture_count;
            return snapshot;
        }
    };

    AssetCatalogNode Node(std::string key, std::string name, std::string type,
                          AssetCatalogAvailability availability =
                              AssetCatalogAvailability::LoadedArchiveProduct)
    {
        AssetCatalogNode node;
        node.stable_key = std::move(key);
        node.display_name = std::move(name);
        node.type_name = std::move(type);
        node.availability = availability;
        node.dependency_coverage = AssetCatalogDependencyCoverage::Complete;
        return node;
    }

    AssetCatalogSnapshot GraphSnapshot(bool cycle = false, bool shared = false,
                                       bool unknown = false)
    {
        AssetCatalogSnapshot snapshot;
        snapshot.revision = 1;
        snapshot.nodes = {Node("level", "Sponza", "Level"),
                          Node("model", "Sponza", "Model"),
                          Node("material", "Bricks", "Material"),
                          Node("texture", "BricksAlbedo", "Texture",
                               AssetCatalogAvailability::ArchiveOnly)};
        if (unknown)
        {
            snapshot.nodes[1].dependency_coverage = AssetCatalogDependencyCoverage::Unknown;
        }
        if (shared)
        {
            snapshot.nodes.push_back(Node("texture-shared", "SharedMask", "Texture",
                                          AssetCatalogAvailability::ArchiveOnly));
        }

        const auto edge = [](std::uint32_t from, std::uint32_t to,
                             AssetCatalogRelation relation, std::uint32_t ordinal,
                             std::string label)
        {
            return AssetCatalogEdge{{from}, {to}, relation, ordinal, std::move(label)};
        };
        snapshot.edges.push_back(edge(0, 1, AssetCatalogRelation::Dependency, 0, "model"));
        snapshot.edges.push_back(edge(1, 2, AssetCatalogRelation::OwnedChild, 0, "material"));
        snapshot.edges.push_back(edge(2, 3, AssetCatalogRelation::Dependency, 0, "albedo"));
        if (shared)
        {
            snapshot.edges.push_back(edge(1, 4, AssetCatalogRelation::Dependency, 1, "shared"));
            snapshot.edges.push_back(edge(0, 4, AssetCatalogRelation::Dependency, 1, "shared"));
        }
        if (cycle)
        {
            snapshot.edges.push_back(edge(3, 0, AssetCatalogRelation::Dependency, 0, "owner"));
        }
        for (std::size_t index = 0; index < snapshot.nodes.size(); ++index)
        {
            snapshot.nodes[index].id.value = static_cast<std::uint32_t>(index);
        }
        return snapshot;
    }

    AssetReferenceViewModel MakeViewModel(FakeSource &source, AssetBrowserModel &browser)
    {
        browser.SetSource(&source);
        EXPECT_TRUE(browser.Refresh());
        return AssetReferenceViewModel{browser};
    }
}

TEST(AssetReferenceViewModelTest, DependenciesPreserveOrderedRelations)
{
    FakeSource source;
    source.snapshot = GraphSnapshot();
    AssetBrowserModel browser;
    AssetReferenceViewModel view = MakeViewModel(source, browser);

    view.SetRoot("level");
    view.ExpandAll();

    ASSERT_GE(view.Rows().size(), 4u);
    EXPECT_EQ(view.Rows()[0].kind, AssetReferenceRowKind::Root);
    EXPECT_EQ(view.Rows()[1].type_name, "Model");
    EXPECT_EQ(view.Rows()[2].kind, AssetReferenceRowKind::Edge);
    EXPECT_EQ(view.Rows()[2].relation_token, "owned-child");
    EXPECT_EQ(view.Rows()[3].type_name, "Texture");
    EXPECT_NE(view.ExportedText().find("owned-child -> Material"), std::string::npos);
    EXPECT_NE(view.ExportedText().find("dependency -> Texture"), std::string::npos);
    EXPECT_EQ(source.capture_count, 1);
}

TEST(AssetReferenceViewModelTest, ReferencersInvertTheForwardEdges)
{
    FakeSource source;
    source.snapshot = GraphSnapshot();
    AssetBrowserModel browser;
    AssetReferenceViewModel view = MakeViewModel(source, browser);

    view.SetRoot("texture");
    view.SetDirection(AssetReferenceDirection::Referencers);
    view.ExpandAll();

    ASSERT_GE(view.Rows().size(), 2u);
    EXPECT_EQ(view.Rows()[1].type_name, "Material");
    EXPECT_EQ(view.Rows()[1].stable_key, "material");
    EXPECT_NE(view.ExportedText().find("Referencers:"), std::string::npos);
    EXPECT_EQ(source.capture_count, 1);
}

TEST(AssetReferenceViewModelTest, SharedCycleAndUnknownFactsAreExplicit)
{
    FakeSource source;
    source.snapshot = GraphSnapshot(true, true, true);
    AssetBrowserModel browser;
    AssetReferenceViewModel view = MakeViewModel(source, browser);
    view.SetRoot("level");
    view.ExpandAll();

    EXPECT_TRUE(std::any_of(view.Rows().begin(), view.Rows().end(),
                            [](const auto &row)
                            { return row.kind == AssetReferenceRowKind::SharedLeaf; }));
    EXPECT_TRUE(std::any_of(view.Rows().begin(), view.Rows().end(),
                            [](const auto &row)
                            { return row.kind == AssetReferenceRowKind::CycleLeaf; }));
    EXPECT_TRUE(std::any_of(view.Rows().begin(), view.Rows().end(),
                            [](const auto &row)
                            { return row.kind == AssetReferenceRowKind::UnknownCoverageLeaf; }));
    EXPECT_NE(view.ExportedText().find("cycle to depth"), std::string::npos);
    EXPECT_NE(view.ExportedText().find("shared; first row"), std::string::npos);
    EXPECT_NE(view.ExportedText().find("additional archive dependencies unknown"),
              std::string::npos);
}

TEST(AssetReferenceViewModelTest, MissingReferenceIsAReadableLeaf)
{
    FakeSource source;
    source.snapshot = GraphSnapshot();
    AssetCatalogNode missing = Node("missing", "MissingTexture", "Texture",
                                    AssetCatalogAvailability::Missing);
    missing.kind = AssetCatalogNodeKind::MissingReference;
    source.snapshot.nodes.push_back(std::move(missing));
    source.snapshot.edges.push_back(
        AssetCatalogEdge{{2}, {4}, AssetCatalogRelation::Dependency, 1, "missing"});
    for (std::size_t index = 0; index < source.snapshot.nodes.size(); ++index)
    {
        source.snapshot.nodes[index].id.value = static_cast<std::uint32_t>(index);
    }

    AssetBrowserModel browser;
    AssetReferenceViewModel view = MakeViewModel(source, browser);
    view.SetRoot("level");
    view.ExpandAll();

    const auto missing_row = std::find_if(
        view.Rows().begin(), view.Rows().end(),
        [](const auto &row) { return row.kind == AssetReferenceRowKind::MissingLeaf; });
    ASSERT_NE(missing_row, view.Rows().end());
    EXPECT_EQ(missing_row->display_name, "MissingTexture");
    EXPECT_EQ(missing_row->state_label, "Missing");
    EXPECT_FALSE(missing_row->has_children);
}

TEST(AssetReferenceViewModelTest, MissingRootIsRetainedAcrossRefresh)
{
    FakeSource source;
    source.snapshot = GraphSnapshot();
    AssetBrowserModel browser;
    AssetReferenceViewModel view = MakeViewModel(source, browser);
    view.SetRoot("later");

    EXPECT_FALSE(view.HasResolvedRoot());
    EXPECT_NE(view.RootDiagnostic().find("not present"), std::string::npos);
    EXPECT_NE(view.ExportedText().find("<missing root>"), std::string::npos);

    source.snapshot.nodes.push_back(Node("later", "Later", "Model"));
    source.snapshot.revision = 2;
    for (std::size_t index = 0; index < source.snapshot.nodes.size(); ++index)
    {
        source.snapshot.nodes[index].id.value = static_cast<std::uint32_t>(index);
    }
    ASSERT_TRUE(browser.Refresh());
    view.Sync();
    EXPECT_TRUE(view.HasResolvedRoot());
    EXPECT_EQ(view.RootKey(), "later");
    EXPECT_EQ(source.capture_count, 2);
}

TEST(AssetReferenceViewModelTest, DepthAndRowLimitsEmitBoundedTruncation)
{
    FakeSource source;
    source.snapshot = GraphSnapshot();
    AssetBrowserModel browser;
    AssetReferenceViewModel view = MakeViewModel(source, browser);
    view.SetRoot("level");
    view.SetLimits({1, 3});
    view.ExpandAll();

    EXPECT_LE(view.Rows().size(), 3u);
    EXPECT_TRUE(std::any_of(view.Rows().begin(), view.Rows().end(),
                            [](const auto &row)
                            { return row.kind == AssetReferenceRowKind::Truncation; }));
    EXPECT_EQ(source.capture_count, 1);
}

TEST(AssetReferenceViewModelTest, TextExportIgnoresTreeExpansionAndIsDeterministic)
{
    FakeSource source;
    source.snapshot = GraphSnapshot(true, true, false);
    AssetBrowserModel browser;
    AssetReferenceViewModel view = MakeViewModel(source, browser);
    view.SetRoot("level");
    const std::string initial = view.ExportedText();

    ASSERT_GE(view.Rows().size(), 2u);
    view.ToggleExpanded(view.Rows()[1].occurrence_key);
    EXPECT_EQ(view.ExportedText(), initial);

    view.SetPresentation(AssetReferencePresentation::Text);
    view.ExpandAll();
    const std::string expanded = view.ExportedText();
    view.CollapseAll();

    EXPECT_EQ(initial, expanded);
    EXPECT_EQ(expanded.back(), '\n');
    EXPECT_EQ(source.capture_count, 1);
}

TEST(AssetReferenceViewModelTest, ResetDropsSnapshotDerivedState)
{
    FakeSource source;
    source.snapshot = GraphSnapshot();
    AssetBrowserModel browser;
    AssetReferenceViewModel view = MakeViewModel(source, browser);
    view.SetRoot("level");
    view.ExpandAll();
    view.Reset();

    EXPECT_FALSE(view.HasSnapshot());
    EXPECT_TRUE(view.RootKey().empty());
    EXPECT_TRUE(view.Rows().empty());
    EXPECT_TRUE(view.ExportedText().empty());
    EXPECT_EQ(source.capture_count, 1);
}
