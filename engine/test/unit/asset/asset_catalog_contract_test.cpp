#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "asset/asset_catalog.h"
#include "support/asset_catalog_fixture.h"

namespace
{
    using kpengine::asset::ArchiveProductType;
    using kpengine::asset::AssetCatalogAvailability;
    using kpengine::asset::AssetCatalogDependencyCoverage;
    using kpengine::asset::AssetCatalogDiagnostic;
    using kpengine::asset::AssetCatalogDiagnosticCode;
    using kpengine::asset::AssetCatalogDiagnosticSeverity;
    using kpengine::asset::AssetCatalogEdge;
    using kpengine::asset::AssetCatalogNode;
    using kpengine::asset::AssetCatalogNodeId;
    using kpengine::asset::AssetCatalogNodeKind;
    using kpengine::asset::AssetCatalogRelation;
    using kpengine::asset::AssetCatalogSnapshot;
    using kpengine::asset::AssetCatalogSnapshotStatus;
    using kpengine::asset::AssetID;
    using kpengine::asset::AssetType;
    using kpengine::asset::CanonicalizeAssetCatalogSnapshot;
    using kpengine::asset::ContentHash;
    using kpengine::asset::MakeArchiveProductCatalogKey;
    using kpengine::asset::MakeMissingReferenceCatalogKey;
    using kpengine::asset::MakeRuntimeIdentityCatalogKey;
    using kpengine::asset::MakeRuntimePathCatalogKey;
    using kpengine::asset::Sha256;
    using kpengine::asset::ValidateAssetCatalogSnapshot;
    using kpengine::asset::test::MakeAssetCatalogContractFixture;

    const AssetCatalogNode *FindNode(const AssetCatalogSnapshot &snapshot,
                                     std::string_view stable_key)
    {
        for (const AssetCatalogNode &node : snapshot.nodes)
        {
            if (node.stable_key == stable_key)
            {
                return &node;
            }
        }
        return nullptr;
    }

    const AssetCatalogNode *FindNodeByDisplayName(const AssetCatalogSnapshot &snapshot,
                                                  std::string_view display_name)
    {
        for (const AssetCatalogNode &node : snapshot.nodes)
        {
            if (node.display_name == display_name)
            {
                return &node;
            }
        }
        return nullptr;
    }

    const AssetCatalogNode *FindTarget(const AssetCatalogSnapshot &snapshot,
                                       const AssetCatalogNode &from,
                                       AssetCatalogRelation relation,
                                       std::uint32_t ordinal)
    {
        for (const AssetCatalogEdge &edge : snapshot.edges)
        {
            if (edge.from == from.id && edge.relation == relation &&
                edge.ordinal == ordinal)
            {
                return &snapshot.nodes[edge.to.value];
            }
        }
        return nullptr;
    }

    std::size_t CountIncoming(const AssetCatalogSnapshot &snapshot,
                              AssetCatalogNodeId id)
    {
        std::size_t count = 0;
        for (const AssetCatalogEdge &edge : snapshot.edges)
        {
            if (edge.to == id)
            {
                ++count;
            }
        }
        return count;
    }

    std::vector<std::string> StableKeysOf(const AssetCatalogSnapshot &snapshot)
    {
        std::vector<std::string> keys;
        keys.reserve(snapshot.nodes.size());
        for (const AssetCatalogNode &node : snapshot.nodes)
        {
            keys.push_back(node.stable_key);
        }
        return keys;
    }

    // The smallest snapshot that satisfies every contract rule, used as the
    // baseline that negative tests break one invariant at a time.
    AssetCatalogSnapshot MakeMinimalSnapshot()
    {
        AssetCatalogSnapshot snapshot;
        snapshot.status = AssetCatalogSnapshotStatus::Complete;

        AssetCatalogNode parent;
        parent.id.value = 0;
        parent.stable_key =
            MakeRuntimePathCatalogKey(AssetType::KPAT_Level, "level/main.level");
        parent.type = AssetType::KPAT_Level;
        parent.type_name = "Level";
        parent.availability = AssetCatalogAvailability::RuntimeOnly;
        parent.dependency_coverage = AssetCatalogDependencyCoverage::Complete;

        AssetCatalogNode child;
        child.id.value = 1;
        child.stable_key =
            MakeRuntimeIdentityCatalogKey(AssetID(7, 1, AssetType::KPAT_Mesh));
        child.type = AssetType::KPAT_Mesh;
        child.type_name = "Mesh";
        child.availability = AssetCatalogAvailability::RuntimeOnly;

        snapshot.nodes.push_back(std::move(parent));
        snapshot.nodes.push_back(std::move(child));

        AssetCatalogEdge edge;
        edge.from.value = 0;
        edge.to.value = 1;
        snapshot.edges.push_back(std::move(edge));

        std::string diagnostic;
        EXPECT_TRUE(CanonicalizeAssetCatalogSnapshot(snapshot, diagnostic))
            << diagnostic;
        return snapshot;
    }

    // Rebuilds an already canonical snapshot with reversed node insertion
    // order and matching remapped endpoints.
    AssetCatalogSnapshot ReverseNodeOrder(const AssetCatalogSnapshot &source)
    {
        const std::uint32_t count = static_cast<std::uint32_t>(source.nodes.size());
        AssetCatalogSnapshot result = source;
        result.nodes.clear();
        for (std::uint32_t index = 0; index < count; ++index)
        {
            AssetCatalogNode node = source.nodes[count - 1 - index];
            node.id.value = index;
            result.nodes.push_back(std::move(node));
        }
        for (AssetCatalogEdge &edge : result.edges)
        {
            edge.from.value = count - 1 - edge.from.value;
            edge.to.value = count - 1 - edge.to.value;
        }
        return result;
    }

    void ExpectSnapshotsEqual(const AssetCatalogSnapshot &lhs,
                              const AssetCatalogSnapshot &rhs)
    {
        ASSERT_EQ(lhs.status, rhs.status);
        ASSERT_EQ(lhs.nodes.size(), rhs.nodes.size());
        ASSERT_EQ(lhs.edges.size(), rhs.edges.size());
        ASSERT_EQ(lhs.diagnostics.size(), rhs.diagnostics.size());
        for (std::size_t index = 0; index < lhs.nodes.size(); ++index)
        {
            EXPECT_EQ(lhs.nodes[index].id, rhs.nodes[index].id);
            EXPECT_EQ(lhs.nodes[index].stable_key, rhs.nodes[index].stable_key);
            EXPECT_EQ(lhs.nodes[index].type_name, rhs.nodes[index].type_name);
            EXPECT_EQ(lhs.nodes[index].availability, rhs.nodes[index].availability);
            EXPECT_EQ(lhs.nodes[index].aliases, rhs.nodes[index].aliases);
            EXPECT_EQ(lhs.nodes[index].provenance.size(),
                      rhs.nodes[index].provenance.size());
        }
        for (std::size_t index = 0; index < lhs.edges.size(); ++index)
        {
            EXPECT_EQ(lhs.edges[index], rhs.edges[index]);
        }
    }

    TEST(AssetCatalogContractTest, NodeIdDefaultsToInvalidAndZeroIsUsable)
    {
        const AssetCatalogNodeId defaulted;
        EXPECT_FALSE(defaulted.IsValid());

        AssetCatalogNodeId assigned;
        assigned.value = 0;
        EXPECT_TRUE(assigned.IsValid());
        EXPECT_EQ(assigned, (AssetCatalogNodeId{0}));
        EXPECT_NE(assigned, defaulted);
    }

    TEST(AssetCatalogContractTest, GoldenKeySpellingsAreStable)
    {
        const ContentHash zero{};
        EXPECT_EQ(MakeArchiveProductCatalogKey(ArchiveProductType::Material, zero),
                  "asset-catalog-v1/product/2/" + std::string(64, '0'));

        const ContentHash hash = Sha256("kpengine.asset-catalog.golden");
        EXPECT_EQ(MakeArchiveProductCatalogKey(ArchiveProductType::Model, hash),
                  "asset-catalog-v1/product/1/" + hash.ToHex());

        // The builder canonicalizes, so separators and case never leak into the
        // frozen spelling.
        EXPECT_EQ(MakeRuntimePathCatalogKey(AssetType::KPAT_Level, "Level\\Main.Level"),
                  "asset-catalog-v1/path/8/level/main.level");

        EXPECT_EQ(MakeRuntimeIdentityCatalogKey(AssetID(0x1u, 0x2u, AssetType::KPAT_Model)),
                  "asset-catalog-v1/runtime/0001000200000001");

        const std::string owner = "asset-catalog-v1/path/8/level/main.level";
        EXPECT_EQ(MakeMissingReferenceCatalogKey(owner, AssetCatalogRelation::Dependency,
                                                 2, AssetType::KPAT_Texture),
                  "asset-catalog-v1/missing/" + Sha256(owner).ToHex() + "/dependency/2/2");
        // Decimal fields have no leading zeroes, including for Undefined.
        EXPECT_EQ(MakeMissingReferenceCatalogKey(owner, AssetCatalogRelation::OwnedChild,
                                                 0, AssetType::Undefined),
                  "asset-catalog-v1/missing/" + Sha256(owner).ToHex() + "/owned-child/0/0");
    }

    TEST(AssetCatalogContractTest, KeyBuildersAreDeterministicAndDistinct)
    {
        const ContentHash hash = Sha256("kpengine.asset-catalog.distinct");
        const std::string product =
            MakeArchiveProductCatalogKey(ArchiveProductType::Texture, hash);
        const std::string path =
            MakeRuntimePathCatalogKey(AssetType::KPAT_Texture, "texture/bricks.texture");
        const std::string runtime =
            MakeRuntimeIdentityCatalogKey(AssetID(9, 1, AssetType::KPAT_Texture));
        const std::string missing =
            MakeMissingReferenceCatalogKey(path, AssetCatalogRelation::Dependency, 0,
                                           AssetType::KPAT_Texture);

        EXPECT_EQ(product, MakeArchiveProductCatalogKey(ArchiveProductType::Texture, hash));
        EXPECT_EQ(path, MakeRuntimePathCatalogKey(AssetType::KPAT_Texture, "texture/bricks.texture"));

        const std::vector<std::string> keys{product, path, runtime, missing};
        for (std::size_t lhs = 0; lhs < keys.size(); ++lhs)
        {
            EXPECT_EQ(keys[lhs].compare(0, std::string_view("asset-catalog-v1/").size(),
                                        "asset-catalog-v1/"),
                      0);
            for (std::size_t rhs = lhs + 1; rhs < keys.size(); ++rhs)
            {
                EXPECT_NE(keys[lhs], keys[rhs]);
            }
        }
    }

    TEST(AssetCatalogContractTest, CanonicalizationIsIndependentOfInsertionOrder)
    {
        const AssetCatalogSnapshot fixture = MakeAssetCatalogContractFixture();
        AssetCatalogSnapshot reversed = ReverseNodeOrder(fixture);

        std::string diagnostic;
        ASSERT_TRUE(CanonicalizeAssetCatalogSnapshot(reversed, diagnostic)) << diagnostic;
        ExpectSnapshotsEqual(fixture, reversed);
    }

    TEST(AssetCatalogContractTest, CanonicalizationRemapsEndpointsAfterNodeSorting)
    {
        const AssetCatalogSnapshot fixture = MakeAssetCatalogContractFixture();
        const AssetCatalogNode *level =
            FindNodeByDisplayName(fixture, "sponza_test.level");
        ASSERT_NE(level, nullptr);
        const AssetCatalogNode *model = FindNodeByDisplayName(fixture, "Sponza");
        ASSERT_NE(model, nullptr);

        const AssetCatalogNode *target =
            FindTarget(fixture, *level, AssetCatalogRelation::Dependency, 0);
        ASSERT_NE(target, nullptr);
        // The Level node sorts second by stable key while keeping a valid dense
        // id, so a stale endpoint mapping would point somewhere else entirely.
        EXPECT_EQ(target->stable_key, model->stable_key);
        EXPECT_EQ(level->id.value, 1u);
        EXPECT_EQ(target->id, model->id);
    }

    TEST(AssetCatalogContractTest, FixtureExposesTheExpectedGraphShape)
    {
        const AssetCatalogSnapshot fixture = MakeAssetCatalogContractFixture();

        ASSERT_EQ(fixture.status, AssetCatalogSnapshotStatus::Partial);
        ASSERT_EQ(fixture.nodes.size(), 8u);
        ASSERT_EQ(fixture.edges.size(), 9u);
        ASSERT_EQ(fixture.diagnostics.size(), 1u);

        const AssetCatalogNode *level =
            FindNodeByDisplayName(fixture, "sponza_test.level");
        const AssetCatalogNode *model = FindNodeByDisplayName(fixture, "Sponza");
        const AssetCatalogNode *mesh = FindNodeByDisplayName(fixture, "sponza_mesh");
        const AssetCatalogNode *stone = FindNodeByDisplayName(fixture, "stone");
        const AssetCatalogNode *fabric = FindNodeByDisplayName(fixture, "fabric");
        const AssetCatalogNode *albedo = FindNodeByDisplayName(fixture, "shared_albedo");
        const AssetCatalogNode *normal = FindNodeByDisplayName(fixture, "fabric_normal");
        const AssetCatalogNode *missing = FindNodeByDisplayName(fixture, "fabric_orm");
        ASSERT_NE(level, nullptr);
        ASSERT_NE(model, nullptr);
        ASSERT_NE(mesh, nullptr);
        ASSERT_NE(stone, nullptr);
        ASSERT_NE(fabric, nullptr);
        ASSERT_NE(albedo, nullptr);
        ASSERT_NE(normal, nullptr);
        ASSERT_NE(missing, nullptr);

        EXPECT_EQ(level->availability, AssetCatalogAvailability::RuntimeOnly);
        EXPECT_EQ(model->availability, AssetCatalogAvailability::LoadedArchiveProduct);
        EXPECT_EQ(mesh->availability, AssetCatalogAvailability::RuntimeOnly);
        EXPECT_EQ(normal->availability, AssetCatalogAvailability::ArchiveOnly);
        EXPECT_EQ(missing->availability, AssetCatalogAvailability::Missing);
        EXPECT_EQ(missing->kind, AssetCatalogNodeKind::MissingReference);

        EXPECT_EQ(FindTarget(fixture, *level, AssetCatalogRelation::Dependency, 0), model);
        EXPECT_EQ(FindTarget(fixture, *model, AssetCatalogRelation::OwnedChild, 0), mesh);
        EXPECT_EQ(FindTarget(fixture, *model, AssetCatalogRelation::Dependency, 0), stone);
        EXPECT_EQ(FindTarget(fixture, *model, AssetCatalogRelation::Dependency, 1), fabric);
        EXPECT_EQ(FindTarget(fixture, *fabric, AssetCatalogRelation::Dependency, 0), albedo);
        EXPECT_EQ(FindTarget(fixture, *fabric, AssetCatalogRelation::Dependency, 1), normal);
        EXPECT_EQ(FindTarget(fixture, *fabric, AssetCatalogRelation::Dependency, 2), missing);
        EXPECT_EQ(FindTarget(fixture, *stone, AssetCatalogRelation::Dependency, 0), albedo);

        // The shared texture stays one node with two incoming edges, and the
        // deliberate cycle is ordinary graph data.
        EXPECT_EQ(CountIncoming(fixture, albedo->id), 2u);
        EXPECT_EQ(FindTarget(fixture, *fabric, AssetCatalogRelation::Dependency, 3), model);
    }

    TEST(AssetCatalogContractTest, OwnedChildrenAreNotDuplicatedAsDependencies)
    {
        const AssetCatalogSnapshot fixture = MakeAssetCatalogContractFixture();
        for (const AssetCatalogEdge &edge : fixture.edges)
        {
            if (edge.relation != AssetCatalogRelation::OwnedChild)
            {
                continue;
            }
            for (const AssetCatalogEdge &other : fixture.edges)
            {
                EXPECT_FALSE(other.from == edge.from && other.to == edge.to &&
                             other.relation == AssetCatalogRelation::Dependency);
            }
        }
    }

    TEST(AssetCatalogContractTest, CustomAssetTypesCarryTheirDescriptorName)
    {
        constexpr AssetType kCustomType = static_cast<AssetType>(0x1000u);
        AssetCatalogSnapshot snapshot = MakeMinimalSnapshot();
        snapshot.nodes[1].type = kCustomType;
        snapshot.nodes[1].type_name = "Live2D Model";
        snapshot.nodes[1].stable_key =
            MakeRuntimePathCatalogKey(kCustomType, "live2d/hiyori.model2");
        snapshot.edges[0].to.value = 1;

        std::string diagnostic;
        ASSERT_TRUE(CanonicalizeAssetCatalogSnapshot(snapshot, diagnostic)) << diagnostic;
        EXPECT_EQ(MakeRuntimePathCatalogKey(kCustomType, "live2d/hiyori.model2"),
                  "asset-catalog-v1/path/4096/live2d/hiyori.model2");
        EXPECT_TRUE(ValidateAssetCatalogSnapshot(snapshot, diagnostic)) << diagnostic;
    }

    TEST(AssetCatalogContractTest, CanonicalizationSortsProvenanceAndAliasesWithoutAddingEdges)
    {
        const AssetCatalogSnapshot fixture = MakeAssetCatalogContractFixture();
        const AssetCatalogNode *model = FindNodeByDisplayName(fixture, "Sponza");
        ASSERT_NE(model, nullptr);

        ASSERT_EQ(model->provenance.size(), 2u);
        EXPECT_EQ(model->provenance[0].source_path, "source/sponza/sponza.mtl");
        EXPECT_EQ(model->provenance[1].source_path, "source/sponza/sponza.obj");
        ASSERT_EQ(model->provenance[0].material_overrides.size(), 2u);
        EXPECT_EQ(model->provenance[0].material_overrides[0].slot, 0);
        EXPECT_EQ(model->provenance[0].material_overrides[0].authored_path, "material/fabric");
        EXPECT_EQ(model->provenance[0].material_overrides[1].slot, 2);

        EXPECT_EQ(model->aliases, (std::vector<std::string>{"Sponza", "model/sponza"}));

        // Nine authored edges only: provenance never becomes graph data.
        EXPECT_EQ(fixture.edges.size(), 9u);
        EXPECT_EQ(fixture.nodes.size(), 8u);
    }

    TEST(AssetCatalogContractTest, CanonicalizationRejectsDuplicateStableKeysAndLeavesInputIntact)
    {
        AssetCatalogSnapshot snapshot = MakeMinimalSnapshot();
        snapshot.nodes[1].stable_key = snapshot.nodes[0].stable_key;
        snapshot.edges.clear();
        const AssetCatalogSnapshot original = snapshot;

        std::string diagnostic;
        EXPECT_FALSE(CanonicalizeAssetCatalogSnapshot(snapshot, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
        ExpectSnapshotsEqual(original, snapshot);
    }

    TEST(AssetCatalogContractTest, CanonicalizationRejectsDuplicateEdgeOrdinals)
    {
        AssetCatalogSnapshot snapshot = MakeMinimalSnapshot();
        snapshot.edges.push_back(snapshot.edges[0]);

        std::string diagnostic;
        EXPECT_FALSE(CanonicalizeAssetCatalogSnapshot(snapshot, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    }

    TEST(AssetCatalogContractTest, ValidatorRejectsInvalidEndpointsAndSelfOwnedChildren)
    {
        std::string diagnostic;

        AssetCatalogSnapshot dangling = MakeMinimalSnapshot();
        dangling.edges[0].to.value = 99;
        EXPECT_FALSE(ValidateAssetCatalogSnapshot(dangling, diagnostic));

        AssetCatalogSnapshot self_owned = MakeMinimalSnapshot();
        self_owned.edges[0].relation = AssetCatalogRelation::OwnedChild;
        self_owned.edges[0].to = self_owned.edges[0].from;
        EXPECT_FALSE(ValidateAssetCatalogSnapshot(self_owned, diagnostic));
    }

    TEST(AssetCatalogContractTest, ValidatorRejectsUnsortedNodesAndAcceptsCanonicalOutput)
    {
        AssetCatalogSnapshot snapshot = MakeMinimalSnapshot();
        snapshot.edges.clear();
        std::swap(snapshot.nodes[0].stable_key, snapshot.nodes[1].stable_key);

        std::string diagnostic;
        EXPECT_FALSE(ValidateAssetCatalogSnapshot(snapshot, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        // Canonicalizing repairs ordering rather than rejecting it.
        ASSERT_TRUE(CanonicalizeAssetCatalogSnapshot(snapshot, diagnostic)) << diagnostic;
        EXPECT_TRUE(ValidateAssetCatalogSnapshot(snapshot, diagnostic)) << diagnostic;
    }

    TEST(AssetCatalogContractTest, ValidatorRejectsEmptyOrdinaryTypeNamesAndMissingTypes)
    {
        std::string diagnostic;

        AssetCatalogSnapshot unnamed = MakeMinimalSnapshot();
        unnamed.nodes[1].type_name.clear();
        EXPECT_FALSE(ValidateAssetCatalogSnapshot(unnamed, diagnostic));

        AssetCatalogSnapshot untyped = MakeMinimalSnapshot();
        untyped.nodes[1].type = AssetType::Undefined;
        EXPECT_FALSE(ValidateAssetCatalogSnapshot(untyped, diagnostic));
    }

    TEST(AssetCatalogContractTest, ValidatorEnforcesAvailabilityAndCoverageCombinations)
    {
        std::string diagnostic;

        AssetCatalogSnapshot loaded_without_coverage = MakeMinimalSnapshot();
        loaded_without_coverage.nodes[0].availability =
            AssetCatalogAvailability::LoadedArchiveProduct;
        loaded_without_coverage.nodes[0].dependency_coverage =
            AssetCatalogDependencyCoverage::Unknown;
        loaded_without_coverage.nodes[0].archive_product_type = ArchiveProductType::Model;
        loaded_without_coverage.nodes[0].product_path = "products/model/x.kpmodel";
        EXPECT_FALSE(ValidateAssetCatalogSnapshot(loaded_without_coverage, diagnostic));

        AssetCatalogSnapshot archive_with_coverage = MakeMinimalSnapshot();
        archive_with_coverage.nodes[0].availability = AssetCatalogAvailability::ArchiveOnly;
        archive_with_coverage.nodes[0].dependency_coverage =
            AssetCatalogDependencyCoverage::Complete;
        archive_with_coverage.nodes[0].archive_product_type = ArchiveProductType::Model;
        archive_with_coverage.nodes[0].product_path = "products/model/x.kpmodel";
        archive_with_coverage.nodes[0].content_hash = ContentHash{};
        EXPECT_FALSE(ValidateAssetCatalogSnapshot(archive_with_coverage, diagnostic));

        AssetCatalogSnapshot archive_without_hash = MakeMinimalSnapshot();
        archive_without_hash.nodes[0].availability = AssetCatalogAvailability::ArchiveOnly;
        archive_without_hash.nodes[0].dependency_coverage =
            AssetCatalogDependencyCoverage::Unknown;
        archive_without_hash.nodes[0].archive_product_type = ArchiveProductType::Model;
        archive_without_hash.nodes[0].product_path = "products/model/x.kpmodel";
        EXPECT_FALSE(ValidateAssetCatalogSnapshot(archive_without_hash, diagnostic));

        AssetCatalogSnapshot runtime_with_product_type = MakeMinimalSnapshot();
        runtime_with_product_type.nodes[0].archive_product_type = ArchiveProductType::Model;
        EXPECT_FALSE(ValidateAssetCatalogSnapshot(runtime_with_product_type, diagnostic));

        // Runtime-only coverage may be either known or unknown.
        AssetCatalogSnapshot runtime_unknown = MakeMinimalSnapshot();
        runtime_unknown.nodes[0].dependency_coverage =
            AssetCatalogDependencyCoverage::Unknown;
        EXPECT_TRUE(ValidateAssetCatalogSnapshot(runtime_unknown, diagnostic)) << diagnostic;
    }

    TEST(AssetCatalogContractTest, MissingReferenceRequiresMissingAvailabilityAndDiagnostic)
    {
        AssetCatalogSnapshot snapshot = MakeMinimalSnapshot();
        const std::string missing_key = MakeMissingReferenceCatalogKey(
            snapshot.nodes[0].stable_key, AssetCatalogRelation::Dependency, 1,
            AssetType::KPAT_Texture);

        AssetCatalogNode missing;
        missing.id.value = 2;
        missing.stable_key = missing_key;
        missing.kind = AssetCatalogNodeKind::MissingReference;
        missing.type = AssetType::KPAT_Texture;
        missing.type_name = "Texture";
        missing.availability = AssetCatalogAvailability::Missing;
        missing.dependency_coverage = AssetCatalogDependencyCoverage::Unknown;
        snapshot.nodes.push_back(std::move(missing));

        AssetCatalogEdge edge;
        edge.from.value = 0;
        edge.to.value = 2;
        edge.ordinal = 1;
        snapshot.edges.push_back(std::move(edge));

        // Without the unresolved diagnostic the missing node is not publishable.
        std::string diagnostic;
        EXPECT_FALSE(CanonicalizeAssetCatalogSnapshot(snapshot, diagnostic));

        AssetCatalogDiagnostic unresolved;
        unresolved.severity = AssetCatalogDiagnosticSeverity::Warning;
        unresolved.code = AssetCatalogDiagnosticCode::UnresolvedDependency;
        unresolved.message = "authored reference has no product or live asset";
        unresolved.related_stable_key = missing_key;
        snapshot.diagnostics.push_back(std::move(unresolved));

        snapshot.status = AssetCatalogSnapshotStatus::Partial;
        ASSERT_TRUE(CanonicalizeAssetCatalogSnapshot(snapshot, diagnostic)) << diagnostic;
        const AssetCatalogNode *node = FindNode(snapshot, missing_key);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->kind, AssetCatalogNodeKind::MissingReference);
        EXPECT_EQ(node->availability, AssetCatalogAvailability::Missing);
        EXPECT_EQ(node->type, AssetType::KPAT_Texture);
    }

    TEST(AssetCatalogContractTest, MissingReferenceKindWithoutMissingAvailabilityIsRejected)
    {
        AssetCatalogSnapshot fixture = MakeAssetCatalogContractFixture();
        for (AssetCatalogNode &node : fixture.nodes)
        {
            if (node.kind == AssetCatalogNodeKind::MissingReference)
            {
                node.availability = AssetCatalogAvailability::RuntimeOnly;
            }
        }
        std::string diagnostic;
        EXPECT_FALSE(ValidateAssetCatalogSnapshot(fixture, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    }

    TEST(AssetCatalogContractTest, DiagnosticReferencingUnknownStableKeyIsRejected)
    {
        AssetCatalogSnapshot snapshot = MakeMinimalSnapshot();
        AssetCatalogDiagnostic orphan;
        orphan.code = AssetCatalogDiagnosticCode::UnknownTypeDescriptor;
        orphan.message = "descriptor missing";
        orphan.related_stable_key = "asset-catalog-v1/path/8/level/absent.level";
        snapshot.diagnostics.push_back(std::move(orphan));

        std::string diagnostic;
        EXPECT_FALSE(ValidateAssetCatalogSnapshot(snapshot, diagnostic));
        EXPECT_FALSE(diagnostic.empty());
    }

    TEST(AssetCatalogContractTest, CompleteSnapshotsRejectErrorDiagnosticsAndPartialOnesAcceptThem)
    {
        AssetCatalogSnapshot snapshot = MakeMinimalSnapshot();
        ASSERT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Complete);

        AssetCatalogDiagnostic failure;
        failure.severity = AssetCatalogDiagnosticSeverity::Error;
        failure.code = AssetCatalogDiagnosticCode::ArchiveUnavailable;
        failure.message = "archive database could not be opened";
        snapshot.diagnostics.push_back(std::move(failure));

        std::string diagnostic;
        EXPECT_FALSE(ValidateAssetCatalogSnapshot(snapshot, diagnostic));
        EXPECT_FALSE(diagnostic.empty());

        // The same usable graph is valid once it is published as partial.
        snapshot.status = AssetCatalogSnapshotStatus::Partial;
        EXPECT_TRUE(ValidateAssetCatalogSnapshot(snapshot, diagnostic)) << diagnostic;
    }

    TEST(AssetCatalogContractTest, SnapshotValuesAreCopyableAndCopiesAreIndependent)
    {
        AssetCatalogSnapshot original = MakeAssetCatalogContractFixture();
        const AssetCatalogSnapshot expected = original;

        AssetCatalogSnapshot copy = original;
        copy.nodes.front().display_name = "mutated";
        copy.nodes.front().aliases.push_back("extra");
        copy.edges.clear();
        copy.nodes.pop_back();

        ExpectSnapshotsEqual(expected, original);
        EXPECT_NE(copy.nodes.front().display_name, original.nodes.front().display_name);
        EXPECT_FALSE(original.edges.empty());

        // Rebuilding the fixture is deterministic.
        ExpectSnapshotsEqual(expected, MakeAssetCatalogContractFixture());
        EXPECT_EQ(StableKeysOf(expected), StableKeysOf(MakeAssetCatalogContractFixture()));
    }

    TEST(AssetCatalogContractTest, FixtureValidatesAsAPartialSnapshot)
    {
        AssetCatalogSnapshot fixture = MakeAssetCatalogContractFixture();
        std::string diagnostic;
        EXPECT_TRUE(ValidateAssetCatalogSnapshot(fixture, diagnostic)) << diagnostic;

        // Canonicalizing an already canonical snapshot is a no-op.
        AssetCatalogSnapshot again = fixture;
        ASSERT_TRUE(CanonicalizeAssetCatalogSnapshot(again, diagnostic)) << diagnostic;
        ExpectSnapshotsEqual(fixture, again);
    }
}
