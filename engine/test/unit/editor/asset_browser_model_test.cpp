#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "editor/asset/asset_browser_model.h"

// The browser model is ImGui-free AND Asset-library-free by design: this target compiles
// the model source directly and links only KP::GoogleTest, so an accidental dependency on
// either is a build error rather than a review finding.
//
// Snapshots are built here by hand rather than taken from the AB1.0 fixture. The fixture is
// fixed, and these are the rules under test — several sizes in one column, duplicate names,
// nested logical paths, aliases and provenance for search. A purpose-built snapshot states
// each case directly, and the model never walks edges, so a non-canonical snapshot is a
// valid input for everything asserted below.
namespace
{
    using kpengine::asset::AssetCatalogAvailability;
    using kpengine::asset::AssetCatalogDiagnostic;
    using kpengine::asset::AssetCatalogDiagnosticCode;
    using kpengine::asset::AssetCatalogDiagnosticSeverity;
    using kpengine::asset::AssetCatalogNode;
    using kpengine::asset::AssetCatalogNodeKind;
    using kpengine::asset::AssetCatalogSnapshot;
    using kpengine::asset::AssetCatalogSnapshotStatus;
    using kpengine::asset::IAssetCatalogSnapshotSource;
    using kpengine::editor::AssetBrowserLocation;
    using kpengine::editor::AssetBrowserModel;
    using kpengine::editor::AssetBrowserRow;
    using kpengine::editor::AssetBrowserPresentation;
    using kpengine::editor::AssetBrowserSortColumn;
    using kpengine::editor::FoldAscii;
    using kpengine::editor::FormatAssetByteSize;

    class FakeSource final : public IAssetCatalogSnapshotSource
    {
    public:
        AssetCatalogSnapshot next;
        int calls = 0;
        bool throw_on_capture = false;

        AssetCatalogSnapshot CaptureAssetCatalog() override
        {
            ++calls;
            if (throw_on_capture)
            {
                throw std::runtime_error("synthetic capture failure");
            }
            return next;
        }
    };

    AssetCatalogNode Node(std::string key, std::string name, std::string type,
                          AssetCatalogAvailability availability, std::uint64_t bytes = 0,
                          std::string logical_path = {})
    {
        AssetCatalogNode node;
        node.stable_key = std::move(key);
        node.display_name = std::move(name);
        node.type_name = std::move(type);
        node.availability = availability;
        node.byte_size = bytes;
        node.logical_path = std::move(logical_path);
        return node;
    }

    AssetCatalogSnapshot SnapshotOf(std::vector<AssetCatalogNode> nodes,
                                    AssetCatalogSnapshotStatus status =
                                        AssetCatalogSnapshotStatus::Complete)
    {
        AssetCatalogSnapshot snapshot;
        snapshot.revision = 1;
        snapshot.status = status;
        snapshot.nodes = std::move(nodes);
        for (std::size_t i = 0; i < snapshot.nodes.size(); ++i)
        {
            snapshot.nodes[i].id.value = static_cast<std::uint32_t>(i);
        }
        return snapshot;
    }

    std::vector<std::string> NamesOf(const AssetBrowserModel &model)
    {
        std::vector<std::string> names;
        for (const auto &row : model.Rows())
        {
            names.push_back(row.display_name);
        }
        return names;
    }

    // The mixed catalog every filter/sort case works against: one of each state, two sizes
    // known and one not, three logical paths sharing a root, and one Runtime-only node.
    FakeSource MixedSource()
    {
        FakeSource source;
        source.next = SnapshotOf({
            Node("k/sponza", "Sponza", "Model", AssetCatalogAvailability::LoadedArchiveProduct,
                 8u * 1024u * 1024u, "model/sponza"),
            Node("k/brick", "Bricks", "Material", AssetCatalogAvailability::ArchiveOnly, 3072,
                 "material/brick"),
            Node("k/tex", "BricksAlbedo", "Texture", AssetCatalogAvailability::ArchiveOnly,
                 0, "texture/brick"),
            Node("k/live", "Untracked", "Model", AssetCatalogAvailability::RuntimeOnly, 512),
            Node("k/gone", "Absent", "Texture", AssetCatalogAvailability::Missing),
        });
        return source;
    }
}

TEST(AssetBrowserModelTest, ANullSourceIsAnUnavailableStateNotACrash)
{
    // Lifecycle tests and a Runtime without scene services both pass null, and the browser
    // has to be a disabled panel rather than an absent one.
    AssetBrowserModel model;

    EXPECT_FALSE(model.IsSourceAvailable());
    EXPECT_FALSE(model.HasSnapshot());
    EXPECT_FALSE(model.Refresh());
    EXPECT_EQ(model.Diagnostic(), "Asset catalog unavailable");
    EXPECT_TRUE(model.Rows().empty());
    EXPECT_EQ(model.SelectedRow(), nullptr);

    // And it recovers: a source arriving clears the message rather than leaving it stale.
    FakeSource source;
    source.next = SnapshotOf({Node("k", "N", "Model", AssetCatalogAvailability::RuntimeOnly)});
    model.SetSource(&source);

    EXPECT_TRUE(model.IsSourceAvailable());
    EXPECT_TRUE(model.Diagnostic().empty());
}

TEST(AssetBrowserModelTest, OpensInCompactTilesWithoutChangingTheCatalogProjection)
{
    FakeSource source = MixedSource();
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    EXPECT_EQ(model.Presentation(), AssetBrowserPresentation::CompactTiles);
    const std::vector<std::string> before = NamesOf(model);

    model.SetPresentation(AssetBrowserPresentation::Table);
    EXPECT_EQ(model.Presentation(), AssetBrowserPresentation::Table);
    EXPECT_EQ(NamesOf(model), before);

    model.SetPresentation(AssetBrowserPresentation::CompactTiles);
    EXPECT_EQ(model.Presentation(), AssetBrowserPresentation::CompactTiles);
    EXPECT_EQ(NamesOf(model), before);
    EXPECT_EQ(source.calls, 1);
}

TEST(AssetBrowserModelTest, RefreshCapturesOnceAndAFrameDoesNotCaptureAtAll)
{
    // The plan's rule: a refresh happens at promotion or on request, and never because a
    // frame elapsed. Query changes rebuild Editor indexes only.
    FakeSource source = MixedSource();
    AssetBrowserModel model;
    model.SetSource(&source);

    ASSERT_TRUE(model.Refresh());
    EXPECT_EQ(source.calls, 1);
    EXPECT_EQ(model.NodeCount(), 5u);
    EXPECT_EQ(model.Revision(), 1u);

    model.SetSearch("brick");
    model.SetLocation(AssetBrowserLocation::ArchiveProducts);
    model.SetSort(AssetBrowserSortColumn::Size, false);
    model.SetPresentation(AssetBrowserPresentation::CompactTiles);
    EXPECT_EQ(source.calls, 1) << "no query change may recapture";

    ASSERT_TRUE(model.Refresh());
    EXPECT_EQ(source.calls, 2) << "an explicit refresh captures exactly once";
}

TEST(AssetBrowserModelTest, APartialSnapshotIsAValidRefresh)
{
    // Partial means usable facts plus recoverable diagnostics. Treating it as a failure
    // would show the user an empty catalog that the provider had actually described.
    FakeSource source = MixedSource();
    source.next.status = AssetCatalogSnapshotStatus::Partial;
    source.next.diagnostics.push_back(AssetCatalogDiagnostic{
        AssetCatalogDiagnosticSeverity::Warning, AssetCatalogDiagnosticCode::ArchiveUnavailable,
        "archive unavailable", {}});
    AssetBrowserModel model;
    model.SetSource(&source);

    ASSERT_TRUE(model.Refresh());

    EXPECT_TRUE(model.SnapshotWasPartial());
    EXPECT_EQ(model.WarningCount(), 1u);
    EXPECT_TRUE(model.Diagnostic().empty()) << "a Partial refresh is still a success";
    EXPECT_EQ(model.Rows().size(), 5u);
}

TEST(AssetBrowserModelTest, AThrowingRefreshKeepsTheLastValidSnapshotAndSelection)
{
    FakeSource source = MixedSource();
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());
    model.Select("k/sponza");
    ASSERT_NE(model.SelectedRow(), nullptr);
    const std::size_t before = model.Rows().size();

    source.throw_on_capture = true;
    EXPECT_FALSE(model.Refresh());

    EXPECT_NE(model.Diagnostic().find("synthetic capture failure"), std::string::npos);
    EXPECT_EQ(model.Rows().size(), before) << "the last valid snapshot survives";
    EXPECT_NE(model.SelectedRow(), nullptr) << "and so does the selection";
    EXPECT_EQ(model.Revision(), 1u) << "a failed refresh does not advance the revision";
}

TEST(AssetBrowserModelTest, ASnapshotWithNoStableKeyOrADuplicateIsRejected)
{
    // The one property this model depends on, since a stable key IS the selection identity.
    FakeSource source = MixedSource();
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());
    ASSERT_EQ(model.Rows().size(), 5u);

    source.next = SnapshotOf(
        {Node("", "Nameless", "Model", AssetCatalogAvailability::RuntimeOnly)});
    EXPECT_FALSE(model.Refresh());
    EXPECT_NE(model.Diagnostic().find("no stable key"), std::string::npos);

    source.next = SnapshotOf({Node("same", "A", "Model", AssetCatalogAvailability::RuntimeOnly),
                              Node("same", "B", "Model", AssetCatalogAvailability::RuntimeOnly)});
    EXPECT_FALSE(model.Refresh());
    EXPECT_NE(model.Diagnostic().find("duplicate stable key"), std::string::npos);

    EXPECT_EQ(model.Rows().size(), 5u) << "neither rejection replaced the good snapshot";
}

TEST(AssetBrowserModelTest, SelectionFollowsItsStableKeyAndOnlyClearsWhenItIsGone)
{
    FakeSource source = MixedSource();
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    model.Select("k/live");
    ASSERT_NE(model.SelectedRow(), nullptr);
    EXPECT_EQ(model.SelectedRow()->display_name, "Untracked");

    // A recapture can renumber dense ids and reorder nodes; the key is what survives.
    std::vector<AssetCatalogNode> reordered = source.next.nodes;
    std::reverse(reordered.begin(), reordered.end());
    source.next = SnapshotOf(std::move(reordered));
    source.next.revision = 2;
    ASSERT_TRUE(model.Refresh());

    ASSERT_NE(model.SelectedRow(), nullptr) << "the key is still present, so selection holds";
    EXPECT_EQ(model.SelectedRow()->display_name, "Untracked");

    source.next = SnapshotOf({Node("k/other", "Other", "Model",
                                   AssetCatalogAvailability::RuntimeOnly)});
    source.next.revision = 3;
    ASSERT_TRUE(model.Refresh());

    EXPECT_EQ(model.SelectedRow(), nullptr) << "the key is gone, so the selection clears";
    EXPECT_TRUE(model.SelectedKey().empty());
}

TEST(AssetBrowserModelTest, SearchFoldsAsciiAndMatchesNamesTypesPathsAliasesAndProvenance)
{
    FakeSource source = MixedSource();
    AssetCatalogNode aliased = Node("k/alias", "Opaque", "Texture",
                                    AssetCatalogAvailability::ArchiveOnly, 10, "texture/x");
    aliased.aliases.push_back("BrickPattern");
    AssetCatalogNode imported = Node("k/import", "Imported", "Model",
                                     AssetCatalogAvailability::ArchiveOnly, 10);
    imported.provenance.push_back(kpengine::asset::AssetCatalogProvenance{
        "model/cerberus/Cerberus_LP.FBX", "Cerberus", {"texture/fur.png"}, {}});
    source.next.nodes.push_back(std::move(aliased));
    source.next.nodes.push_back(std::move(imported));

    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    model.SetSearch("BRICK");
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Bricks", "BricksAlbedo", "Opaque"}))
        << "case folded across name, alias and path";

    model.SetSearch("brickpattern");
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Opaque"})) << "aliases are searchable";

    model.SetSearch("cerberus_lp.fbx");
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Imported"}))
        << "a user searches for the file they imported, not its display name";

    model.SetSearch("texture/");
    EXPECT_EQ(NamesOf(model),
              (std::vector<std::string>{"BricksAlbedo", "Imported", "Opaque"}))
        << "logical paths and provenance dependency paths are searchable; Absent has "
           "neither to match";

    model.SetSearch("model mat");
    EXPECT_TRUE(model.Rows().empty()) << "terms are ANDed, so unrelated words find nothing";

    model.SetSearch("brick material");
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Bricks"}))
        << "both terms must appear, in any order";
}

TEST(AssetBrowserModelTest, NonAsciiSearchBytesAreComparedUnchanged)
{
    // Locale-independent by construction: only A-Z fold, so UTF-8 must still match exactly
    // rather than through a locale that might case-fold it differently.
    FakeSource source;
    source.next = SnapshotOf({Node("k/utf", "\xE6\xA8\xA1\xE5\x9E\x8B", "Model",
                                   AssetCatalogAvailability::ArchiveOnly, 1)});
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    model.SetSearch("\xE6\xA8\xA1\xE5\x9E\x8B");
    EXPECT_EQ(model.Rows().size(), 1u) << "an exact byte sequence matches";

    model.SetSearch("\xE6\xA8\xA1\xE5\x9E\x8C");
    EXPECT_TRUE(model.Rows().empty()) << "a differing byte does not";
}

TEST(AssetBrowserModelTest, LocationsPartitionTheCatalogByAvailability)
{
    FakeSource source = MixedSource();
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    EXPECT_EQ(model.Rows().size(), 5u) << "All is the default";

    model.SetLocation(AssetBrowserLocation::ArchiveProducts);
    EXPECT_EQ(NamesOf(model),
              (std::vector<std::string>{"Bricks", "BricksAlbedo", "Sponza"}))
        << "Archive Products is ArchiveOnly plus LoadedArchiveProduct";

    model.SetLocation(AssetBrowserLocation::RuntimeOnly);
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Untracked"}));

    model.SetLocation(AssetBrowserLocation::Missing);
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Absent"}));
}

TEST(AssetBrowserModelTest, TypeAndAvailabilityFiltersAreOrWithinAndAndBetween)
{
    FakeSource source = MixedSource();
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    const std::vector<std::string> &types = model.TypeNames();
    EXPECT_EQ(types, (std::vector<std::string>{"Material", "Model", "Texture"}))
        << "distinct type names come from the snapshot, sorted bytewise";

    model.SetTypeFilter({"Material", "Texture"});
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Absent", "Bricks", "BricksAlbedo"}))
        << "OR within the type group";

    model.SetAvailabilityFilter({AssetCatalogAvailability::ArchiveOnly});
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Bricks", "BricksAlbedo"}))
        << "AND between groups";

    model.SetTypeFilter({});
    model.SetAvailabilityFilter({});
    EXPECT_EQ(model.Rows().size(), 5u) << "empty groups mean all";

    // The type list survives its own filter being applied, so the control cannot empty.
    EXPECT_EQ(model.TypeNames().size(), 3u);
}

TEST(AssetBrowserModelTest, TheLogicalPrefixSelectsAWholeSubtreeByComponent)
{
    FakeSource source = MixedSource();
    source.next.nodes.push_back(Node("k/brick/deep", "Deep", "Texture",
                                     AssetCatalogAvailability::ArchiveOnly, 1,
                                     "material/brick/trim"));
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    model.SetLogicalPrefix("material/");
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Bricks", "Deep"}));

    model.SetLogicalPrefix("material/brick");
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Bricks", "Deep"}))
        << "a prefix matches at a component boundary";

    model.SetLogicalPrefix("material/bric");
    EXPECT_TRUE(model.Rows().empty())
        << "a partial component must not match: 'bric' is not 'brick'";

    model.SetLogicalPrefix("model");
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Sponza"}));
}

TEST(AssetBrowserModelTest, SelectingAFolderKeepsSiblingFoldersAvailable)
{
    FakeSource source = MixedSource();
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    model.SetLogicalPrefix("model");
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Sponza"}));

    std::vector<std::string> folders;
    for (const auto &folder : model.Folders())
    {
        folders.push_back(folder.path);
    }
    EXPECT_EQ(folders, (std::vector<std::string>{"material", "model", "texture"}));
}

TEST(AssetBrowserModelTest, FoldersDeriveFromTheFilteredRowsWithCounts)
{
    FakeSource source = MixedSource();
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    std::vector<std::string> paths;
    for (const auto &folder : model.Folders())
    {
        paths.push_back(folder.path + "=" + std::to_string(folder.count));
    }
    // A folder exists for a DIRECTORY, so "material/brick" is a file in "material" and
    // contributes no "material/brick" folder of its own.
    EXPECT_EQ(paths, (std::vector<std::string>{"material=1", "model=1", "texture=1"}))
        << "counts are per directory prefix and the order is bytewise";

    // Counts follow what is on screen, so a filtered view cannot claim nodes it is hiding.
    model.SetSearch("brick");
    paths.clear();
    for (const auto &folder : model.Folders())
    {
        paths.push_back(folder.path + "=" + std::to_string(folder.count));
    }
    EXPECT_EQ(paths, (std::vector<std::string>{"material=1", "texture=1"}))
        << "the model folder dropped out with its only row";
}

TEST(AssetBrowserModelTest, FoldersOnlyExposeTopLevelContentCategories)
{
    FakeSource source = MixedSource();
    source.next.nodes.push_back(Node(
        "k/absolute", "DeepModel", "Model", AssetCatalogAvailability::ArchiveOnly, 1,
        "D:/C++Project/KimPeanutEngine/content/model/nested/deep"));
    source.next.nodes.push_back(Node(
        "k/level", "TestLevel", "Level", AssetCatalogAvailability::RuntimeOnly, 1,
        "content/level/validation"));
    source.next.nodes.push_back(Node(
        "k/archive", "InternalProduct", "Model", AssetCatalogAvailability::ArchiveOnly, 1,
        "content/.archive/models/hash"));
    source.next.nodes.push_back(Node(
        "k/shader", "InternalShader", "Shader", AssetCatalogAvailability::ArchiveOnly, 1,
        "content/shader/pbr"));
    source.next.nodes.push_back(Node(
        "k/other", "OtherFolder", "Material", AssetCatalogAvailability::ArchiveOnly, 1,
        "content/other/nested"));

    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    std::vector<std::string> paths;
    for (const auto &folder : model.Folders())
    {
        paths.push_back(folder.path + "=" + std::to_string(folder.count));
    }
    EXPECT_EQ(paths, (std::vector<std::string>{"level=1", "material=1", "model=2", "texture=1"}));
    EXPECT_EQ(std::count_if(model.Rows().begin(), model.Rows().end(),
                               [](const AssetBrowserRow &row)
                               { return row.stable_key == "k/shader"; }), 0);
    EXPECT_EQ(std::find(model.TypeNames().begin(), model.TypeNames().end(), "Shader"),
              model.TypeNames().end());
    const auto deep = std::find_if(model.Rows().begin(), model.Rows().end(),
                                   [](const AssetBrowserRow &row)
                                   { return row.display_name == "DeepModel"; });
    ASSERT_NE(deep, model.Rows().end());
    EXPECT_EQ(deep->logical_path, "model/nested/deep");
}

TEST(AssetBrowserModelTest, SortingIsDeterministicInBothDirectionsWithStableTieBreakers)
{
    FakeSource source;
    source.next = SnapshotOf({
        // Two share a display name, so the tie-breakers are what decide.
        Node("k/b", "Same", "Model", AssetCatalogAvailability::ArchiveOnly, 300),
        Node("k/a", "Same", "Material", AssetCatalogAvailability::RuntimeOnly, 100),
        Node("k/c", "Alpha", "Texture", AssetCatalogAvailability::Missing),
        Node("k/d", "Zulu", "Model", AssetCatalogAvailability::ArchiveOnly, 200),
    });
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    model.SetSort(AssetBrowserSortColumn::Name, true);
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Alpha", "Same", "Same", "Zulu"}));

    model.SetSort(AssetBrowserSortColumn::Name, false);
    EXPECT_EQ(NamesOf(model), (std::vector<std::string>{"Zulu", "Same", "Same", "Alpha"}));

    // Equal names keep one order in both directions: the tie-breakers never invert.
    model.SetSort(AssetBrowserSortColumn::Name, true);
    const std::vector<std::string> ascending = NamesOf(model);
    model.SetSort(AssetBrowserSortColumn::Name, false);
    const std::vector<std::string> descending = NamesOf(model);
    EXPECT_EQ(ascending[1], descending[2]);
    EXPECT_EQ(ascending[2], descending[1]);

    model.SetSort(AssetBrowserSortColumn::Size, true);
    EXPECT_EQ(model.Rows().front().size_label, "-")
        << "an unknown size sorts first ascending";
    EXPECT_EQ(model.Rows().back().size_label, "300 B")
        << "and the largest size is last, whichever node carries it";

    model.SetSort(AssetBrowserSortColumn::Size, false);
    EXPECT_EQ(model.Rows().back().size_label, "-")
        << "the unknown size goes last descending";
    EXPECT_EQ(model.Rows().front().size_label, "300 B");
}

TEST(AssetBrowserModelTest, ClickingTheSortColumnFlipsOnlyThatColumn)
{
    FakeSource source = MixedSource();
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    model.ToggleSort(AssetBrowserSortColumn::Type);
    EXPECT_EQ(model.Query().sort_column, AssetBrowserSortColumn::Type);
    EXPECT_TRUE(model.Query().ascending) << "a new column starts ascending";

    model.ToggleSort(AssetBrowserSortColumn::Type);
    EXPECT_FALSE(model.Query().ascending) << "the same column flips";

    model.ToggleSort(AssetBrowserSortColumn::Name);
    EXPECT_TRUE(model.Query().ascending) << "a different column starts ascending again";
}

TEST(AssetBrowserModelTest, KeyboardNavigationWalksTheVisibleProjectionAndClamps)
{
    FakeSource source = MixedSource();
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());
    ASSERT_EQ(model.Rows().size(), 5u);

    EXPECT_EQ(model.MoveSelection(1), std::optional<std::size_t>{0u})
        << "with nothing selected it starts at the top";

    EXPECT_EQ(model.MoveSelection(1), std::optional<std::size_t>{1u});
    EXPECT_EQ(model.MoveSelection(-1), std::optional<std::size_t>{0u});
    EXPECT_EQ(model.MoveSelection(-1), std::optional<std::size_t>{0u}) << "clamped at the top";
    EXPECT_EQ(model.MoveSelection(99), std::optional<std::size_t>{4u}) << "clamped at the end";
    EXPECT_EQ(model.SelectedRow()->display_name, NamesOf(model).back());

    // An empty projection has nothing to select, and must not leave a stale selection.
    model.SetSearch("nothing matches this");
    EXPECT_TRUE(model.Rows().empty());
    EXPECT_FALSE(model.MoveSelection(1).has_value());
}

TEST(AssetBrowserModelTest, StateLabelsAreExactlyTheFrozenFour)
{
    EXPECT_EQ(AssetBrowserModel::StateLabel(AssetCatalogAvailability::LoadedArchiveProduct),
              "Loaded");
    EXPECT_EQ(AssetBrowserModel::StateLabel(AssetCatalogAvailability::ArchiveOnly), "Archive");
    EXPECT_EQ(AssetBrowserModel::StateLabel(AssetCatalogAvailability::RuntimeOnly), "Runtime");
    EXPECT_EQ(AssetBrowserModel::StateLabel(AssetCatalogAvailability::Missing), "Missing");
}

TEST(AssetBrowserModelTest, RowsCarryReadableTextNextToEveryState)
{
    // Color is never the only signal, so the row must be self-describing without the view.
    FakeSource source = MixedSource();
    AssetBrowserModel model;
    model.SetSource(&source);
    ASSERT_TRUE(model.Refresh());

    for (const auto &row : model.Rows())
    {
        EXPECT_FALSE(row.display_name.empty());
        EXPECT_FALSE(row.type_name.empty());
        EXPECT_FALSE(row.state_label.empty());
        EXPECT_FALSE(row.stable_key.empty());
    }

    model.SetSort(AssetBrowserSortColumn::Name, true);
    const auto found = std::find_if(model.Rows().begin(), model.Rows().end(),
                                    [](const kpengine::editor::AssetBrowserRow &row)
                                    { return row.display_name == "Sponza"; });
    ASSERT_NE(found, model.Rows().end());
    EXPECT_EQ(found->state_label, "Loaded");
    EXPECT_EQ(found->size_label, "8.0 MB");
    EXPECT_EQ(found->logical_path, "model/sponza");
}

TEST(AssetBrowserSizeFormatTest, UsesBinaryUnitsAndADashForAnUnknownSize)
{
    EXPECT_EQ(FormatAssetByteSize(std::nullopt), "-");
    EXPECT_EQ(FormatAssetByteSize(0), "0 B");
    EXPECT_EQ(FormatAssetByteSize(1023), "1023 B");
    EXPECT_EQ(FormatAssetByteSize(1024), "1.0 KB");
    EXPECT_EQ(FormatAssetByteSize(3072), "3.0 KB");
    EXPECT_EQ(FormatAssetByteSize(8u * 1024u * 1024u), "8.0 MB");
    EXPECT_EQ(FormatAssetByteSize(1024ull * 1024ull * 1024ull), "1.0 GB");
    // Three significant digits once past 100, so a column of sizes stays aligned.
    EXPECT_EQ(FormatAssetByteSize(200u * 1024u), "200 KB");
}

TEST(AssetBrowserModelTest, AsciiFoldingLeavesEverythingAboveTheAsciiRangeAlone)
{
    EXPECT_EQ(FoldAscii("AbC"), "abc");
    EXPECT_EQ(FoldAscii("a-b_c.1"), "a-b_c.1");
    // A byte at or above 0x80 is passed through, so UTF-8 is never reinterpreted.
    const std::string utf8 = "\xE6\xA8\xA1\xE5\x9E\x8B";
    EXPECT_EQ(FoldAscii(utf8), utf8);
}
