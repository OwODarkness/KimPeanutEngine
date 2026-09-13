#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "asset/asset_product.h"
#include "asset/detail/asset_catalog_builder.h"
#include "asset/utility.h"

// The builder is a pure function of copied values. Every case here supplies a
// hand-built archive catalog and hand-built live records, so joins, relations,
// limits, and failures are asserted without SQLite, AssetManager, or a lock.
namespace
{
    using kpengine::asset::ArchiveCatalogSource;
    using kpengine::asset::ArchiveProductType;
    using kpengine::asset::AssetCatalogAvailability;
    using kpengine::asset::AssetCatalogDependencyCoverage;
    using kpengine::asset::AssetCatalogDiagnostic;
    using kpengine::asset::AssetCatalogDiagnosticCode;
    using kpengine::asset::AssetCatalogDiagnosticSeverity;
    using kpengine::asset::AssetCatalogEdge;
    using kpengine::asset::AssetCatalogNode;
    using kpengine::asset::AssetCatalogNodeKind;
    using kpengine::asset::AssetCatalogProvenance;
    using kpengine::asset::AssetCatalogRelation;
    using kpengine::asset::AssetCatalogSnapshot;
    using kpengine::asset::AssetCatalogSnapshotStatus;
    using kpengine::asset::AssetID;
    using kpengine::asset::AssetType;
    using kpengine::asset::CanonicalAssetPathKey;
    using kpengine::asset::ContentHash;
    using kpengine::asset::MakeArchiveProductCatalogKey;
    using kpengine::asset::MakeMissingReferenceCatalogKey;
    using kpengine::asset::MakeRuntimeIdentityCatalogKey;
    using kpengine::asset::MakeRuntimePathCatalogKey;
    using kpengine::asset::MaterialOverrideRecord;
    using kpengine::asset::ModelArchiveCatalogSnapshot;
    using kpengine::asset::ProductRecord;
    using kpengine::asset::ProductRelativePath;
    using kpengine::asset::Sha256;
    using kpengine::asset::SourceImportStatus;
    using kpengine::asset::SourceProductRecord;
    using kpengine::asset::detail::AssetCatalogBuildInput;
    using kpengine::asset::detail::BuildAssetCatalog;
    using kpengine::asset::detail::LiveCatalogRecord;

    constexpr std::uint32_t kNoNode = std::numeric_limits<std::uint32_t>::max();

    const ContentHash kModelHash = Sha256("kpengine.catalog-builder/model");
    const ContentHash kStoneHash = Sha256("kpengine.catalog-builder/material/stone");
    const ContentHash kAlbedoHash = Sha256("kpengine.catalog-builder/texture/albedo");

    std::filesystem::path AssetRoot()
    {
        // Purely lexical: the builder never touches the filesystem.
        return std::filesystem::path("D:/kp_catalog_builder_test/assets");
    }

    std::filesystem::path ArchiveRoot()
    {
        return AssetRoot() / ".archive";
    }

    std::string RelativeProductPath(ArchiveProductType type, const ContentHash &hash)
    {
        return ProductRelativePath(type, hash, "kptexture");
    }

    std::string ProductFullPath(ArchiveProductType type, const ContentHash &hash)
    {
        return (ArchiveRoot() / RelativeProductPath(type, hash)).generic_string();
    }

    std::string AssetPath(std::string_view relative)
    {
        return (AssetRoot() / std::filesystem::path(relative)).generic_string();
    }

    ProductRecord MakeProduct(ArchiveProductType type, const ContentHash &hash,
                              std::uint64_t byte_size = 4096, std::uint32_t schema_version = 1)
    {
        ProductRecord product;
        product.asset_type = type;
        product.content_hash = hash;
        product.relative_path = RelativeProductPath(type, hash);
        product.byte_size = byte_size;
        product.schema_version = schema_version;
        return product;
    }

    ArchiveCatalogSource MakeSource(std::string normalized_path, std::string display_name,
                                    SourceImportStatus status = SourceImportStatus::Ready)
    {
        ArchiveCatalogSource source;
        source.source.normalized_path = std::move(normalized_path);
        source.source.display_name = std::move(display_name);
        source.source.status = status;
        return source;
    }

    void LinkProduct(ArchiveCatalogSource &source, ArchiveProductType type,
                     const ContentHash &hash, std::string display_name,
                     std::int32_t slot = -1)
    {
        SourceProductRecord link;
        link.asset_type = type;
        link.content_hash = hash;
        link.display_name = std::move(display_name);
        link.slot = slot;
        source.source_products.push_back(std::move(link));
    }

    std::string DefaultTypeName(AssetType type)
    {
        switch (type)
        {
        case AssetType::KPAT_Model:
            return "Model";
        case AssetType::KPAT_Material:
            return "Material";
        case AssetType::KPAT_Texture:
            return "Texture";
        case AssetType::KPAT_Mesh:
            return "Mesh";
        case AssetType::KPAT_Level:
            return "Level";
        default:
            // A type with no built-in descriptor keeps an empty name so the
            // builder has to fall back to its hexadecimal spelling.
            return {};
        }
    }

    // `path_indexed` mirrors the manager path index and is independent of the
    // path text, which is why it is never derived here.
    LiveCatalogRecord MakeLive(std::uint32_t id, AssetType type, std::string name,
                               std::string path, bool path_indexed = false,
                               std::string type_name = {})
    {
        LiveCatalogRecord record;
        record.id = AssetID(id, 1, type);
        record.type = type;
        record.type_name = type_name.empty() ? DefaultTypeName(type) : std::move(type_name);
        record.name = std::move(name);
        record.path = std::move(path);
        record.path_indexed = path_indexed;
        return record;
    }

    AssetCatalogBuildInput BaseInput()
    {
        AssetCatalogBuildInput input;
        input.revision = 1;
        input.asset_root = AssetRoot();
        input.archive_root = ArchiveRoot();
        input.type_names = {{AssetType::KPAT_Model, "Model"},
                            {AssetType::KPAT_Material, "Material"},
                            {AssetType::KPAT_Texture, "Texture"},
                            {AssetType::KPAT_Mesh, "Mesh"},
                            {AssetType::KPAT_Level, "Level"}};
        return input;
    }

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

    std::uint32_t NodeIndexOf(const AssetCatalogSnapshot &snapshot, std::string_view stable_key)
    {
        for (std::size_t index = 0; index < snapshot.nodes.size(); ++index)
        {
            if (snapshot.nodes[index].stable_key == stable_key)
            {
                return static_cast<std::uint32_t>(index);
            }
        }
        return kNoNode;
    }

    std::vector<const AssetCatalogEdge *> EdgesFrom(const AssetCatalogSnapshot &snapshot,
                                                    std::string_view stable_key)
    {
        std::vector<const AssetCatalogEdge *> result;
        const std::uint32_t from = NodeIndexOf(snapshot, stable_key);
        if (from == kNoNode)
        {
            return result;
        }
        for (const AssetCatalogEdge &edge : snapshot.edges)
        {
            if (edge.from.value == from)
            {
                result.push_back(&edge);
            }
        }
        return result;
    }

    std::size_t CountDiagnostics(const AssetCatalogSnapshot &snapshot,
                                 AssetCatalogDiagnosticCode code)
    {
        return static_cast<std::size_t>(std::count_if(
            snapshot.diagnostics.begin(), snapshot.diagnostics.end(),
            [code](const AssetCatalogDiagnostic &entry) { return entry.code == code; }));
    }

    // Confirms the published snapshot is contract-valid and free of the
    // assembly-failure escape hatch.
    void ExpectValid(const AssetCatalogSnapshot &snapshot)
    {
        std::string diagnostic;
        EXPECT_TRUE(kpengine::asset::ValidateAssetCatalogSnapshot(snapshot, diagnostic))
            << diagnostic;
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::CatalogAssemblyFailed),
                  0u);
    }

    const AssetCatalogProvenance *FindProvenance(const AssetCatalogNode &node,
                                                 std::string_view source_path)
    {
        for (const AssetCatalogProvenance &entry : node.provenance)
        {
            if (entry.source_path == source_path)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    bool SameBytes(const AssetCatalogSnapshot &lhs, const AssetCatalogSnapshot &rhs)
    {
        if (lhs.status != rhs.status || lhs.nodes.size() != rhs.nodes.size() ||
            lhs.edges.size() != rhs.edges.size() ||
            lhs.diagnostics.size() != rhs.diagnostics.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < lhs.nodes.size(); ++index)
        {
            const AssetCatalogNode &left = lhs.nodes[index];
            const AssetCatalogNode &right = rhs.nodes[index];
            if (left.id != right.id || left.stable_key != right.stable_key ||
                left.kind != right.kind || left.type != right.type ||
                left.type_name != right.type_name || left.display_name != right.display_name ||
                left.logical_path != right.logical_path ||
                left.product_path != right.product_path ||
                left.availability != right.availability ||
                left.dependency_coverage != right.dependency_coverage ||
                left.packed_runtime_asset_id != right.packed_runtime_asset_id ||
                left.archive_product_type != right.archive_product_type ||
                left.content_hash != right.content_hash || left.byte_size != right.byte_size ||
                left.schema_version != right.schema_version ||
                left.aliases != right.aliases || left.provenance.size() != right.provenance.size())
            {
                return false;
            }
            for (std::size_t entry = 0; entry < left.provenance.size(); ++entry)
            {
                const AssetCatalogProvenance &lv = left.provenance[entry];
                const AssetCatalogProvenance &rv = right.provenance[entry];
                if (lv.source_path != rv.source_path ||
                    lv.source_display_name != rv.source_display_name ||
                    lv.source_dependency_paths != rv.source_dependency_paths ||
                    lv.material_overrides.size() != rv.material_overrides.size())
                {
                    return false;
                }
                for (std::size_t slot = 0; slot < lv.material_overrides.size(); ++slot)
                {
                    if (lv.material_overrides[slot].slot != rv.material_overrides[slot].slot ||
                        lv.material_overrides[slot].authored_path !=
                            rv.material_overrides[slot].authored_path)
                    {
                        return false;
                    }
                }
            }
        }
        for (std::size_t index = 0; index < lhs.edges.size(); ++index)
        {
            if (lhs.edges[index] != rhs.edges[index])
            {
                return false;
            }
        }
        for (std::size_t index = 0; index < lhs.diagnostics.size(); ++index)
        {
            const AssetCatalogDiagnostic &left = lhs.diagnostics[index];
            const AssetCatalogDiagnostic &right = rhs.diagnostics[index];
            if (left.severity != right.severity || left.code != right.code ||
                left.message != right.message ||
                left.related_stable_key != right.related_stable_key)
            {
                return false;
            }
        }
        return true;
    }

    TEST(AssetCatalogBuilderTest, ArchiveOnlyProductsHaveUnknownCoverageAndReadableFallback)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Texture, kAlbedoHash, 8192));

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Complete);
        EXPECT_TRUE(snapshot.diagnostics.empty());
        ASSERT_EQ(snapshot.nodes.size(), 1u);
        EXPECT_TRUE(snapshot.edges.empty());

        const AssetCatalogNode &node = snapshot.nodes[0];
        EXPECT_EQ(node.id.value, 0u);
        EXPECT_EQ(node.stable_key,
                  MakeArchiveProductCatalogKey(ArchiveProductType::Texture, kAlbedoHash));
        EXPECT_EQ(node.kind, AssetCatalogNodeKind::Asset);
        EXPECT_EQ(node.type, AssetType::KPAT_Texture);
        EXPECT_EQ(node.type_name, "Texture");
        // No linked source names the product, so the fallback is honest
        // about being a type plus a short hash.
        EXPECT_EQ(node.display_name, "Texture " + kAlbedoHash.ToHex().substr(0, 8));
        EXPECT_EQ(node.availability, AssetCatalogAvailability::ArchiveOnly);
        EXPECT_EQ(node.dependency_coverage, AssetCatalogDependencyCoverage::Unknown);
        EXPECT_FALSE(node.packed_runtime_asset_id.has_value());
        ASSERT_TRUE(node.archive_product_type.has_value());
        EXPECT_EQ(*node.archive_product_type, ArchiveProductType::Texture);
        ASSERT_TRUE(node.content_hash.has_value());
        EXPECT_TRUE(*node.content_hash == kAlbedoHash);
        EXPECT_EQ(node.byte_size, 8192u);
        EXPECT_EQ(node.schema_version, 1u);
        EXPECT_EQ(node.product_path, ".archive/textures/" + kAlbedoHash.ToHex() + ".kptexture");
        // Archive storage is internal and never becomes a logical path.
        EXPECT_TRUE(node.logical_path.empty());
        EXPECT_TRUE(node.aliases.empty());
        EXPECT_TRUE(node.provenance.empty());
    }

    TEST(AssetCatalogBuilderTest, RootModelPrefersTheFirstLogicalPathAndSmallestAlias)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Model, kModelHash));

        // Deliberately unsorted: two sources import the same Model product.
        ArchiveCatalogSource zebra = MakeSource("source/zebra/zebra.obj", "zebra.obj");
        LinkProduct(zebra, ArchiveProductType::Model, kModelHash, "beta_model");
        MaterialOverrideRecord dropped;
        dropped.slot = 0;
        MaterialOverrideRecord kept;
        kept.slot = 2;
        kept.authored_path = "material/stone";
        zebra.material_overrides = {dropped, kept};
        archive.sources.push_back(std::move(zebra));

        ArchiveCatalogSource alpha = MakeSource("source/alpha/alpha.obj", "alpha.obj");
        LinkProduct(alpha, ArchiveProductType::Model, kModelHash, "alpha_model");
        archive.sources.push_back(std::move(alpha));

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Complete);
        ASSERT_EQ(snapshot.nodes.size(), 1u);

        const AssetCatalogNode &node = snapshot.nodes[0];
        // The smallest linked name wins, not the first row seen.
        EXPECT_EQ(node.display_name, "alpha_model");
        EXPECT_EQ(node.aliases, (std::vector<std::string>{"alpha_model", "beta_model"}));
        // Only the root Model derives a logical path from its source file.
        EXPECT_EQ(node.logical_path, "source/alpha/alpha");
        ASSERT_EQ(node.provenance.size(), 2u);

        const AssetCatalogProvenance *from_zebra = FindProvenance(node, "source/zebra/zebra.obj");
        ASSERT_NE(from_zebra, nullptr);
        EXPECT_EQ(from_zebra->source_display_name, "zebra.obj");
        // An empty authored path has no representable override.
        ASSERT_EQ(from_zebra->material_overrides.size(), 1u);
        EXPECT_EQ(from_zebra->material_overrides[0].slot, 2);
        EXPECT_EQ(from_zebra->material_overrides[0].authored_path, "material/stone");

        const AssetCatalogProvenance *from_alpha = FindProvenance(node, "source/alpha/alpha.obj");
        ASSERT_NE(from_alpha, nullptr);
        EXPECT_EQ(from_alpha->source_display_name, "alpha.obj");
    }

    TEST(AssetCatalogBuilderTest, RootModelUsesTheOwningSourceNameWhenNoLinkNameExists)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Model, kModelHash));

        ArchiveCatalogSource source = MakeSource("source/sponza/sponza.obj", "sponza.obj");
        LinkProduct(source, ArchiveProductType::Model, kModelHash, "");
        archive.sources.push_back(std::move(source));

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        ASSERT_EQ(snapshot.nodes.size(), 1u);
        EXPECT_EQ(snapshot.nodes[0].display_name, "sponza.obj");
        EXPECT_TRUE(snapshot.nodes[0].aliases.empty());
    }

    TEST(AssetCatalogBuilderTest, ArchiveAliasesAreSortedDeduplicatedAndUniquePerSource)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Material, kStoneHash));

        ArchiveCatalogSource first = MakeSource("source/stone/stone.mtl", "stone.mtl");
        LinkProduct(first, ArchiveProductType::Material, kStoneHash, "stone", 0);
        archive.sources.push_back(std::move(first));

        ArchiveCatalogSource second = MakeSource("source/wall/wall.mtl", "wall.mtl");
        LinkProduct(second, ArchiveProductType::Material, kStoneHash, "stone_v2", 0);
        archive.sources.push_back(std::move(second));

        ArchiveCatalogSource third = MakeSource("source/floor/floor.mtl", "floor.mtl");
        LinkProduct(third, ArchiveProductType::Material, kStoneHash, "stone", 0);
        archive.sources.push_back(std::move(third));

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        ASSERT_EQ(snapshot.nodes.size(), 1u);
        const AssetCatalogNode &node = snapshot.nodes[0];
        EXPECT_EQ(node.display_name, "stone");
        EXPECT_EQ(node.aliases, (std::vector<std::string>{"stone", "stone_v2"}));
        // One provenance entry per importing source, even though the smallest
        // alias came from two of them.
        EXPECT_EQ(node.provenance.size(), 3u);
        // A non-root product never invents a logical path.
        EXPECT_TRUE(node.logical_path.empty());
    }

    TEST(AssetCatalogBuilderTest, MatchingPathAndTypeMergesLiveIdentityIntoTheArchiveNode)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Model, kModelHash, 2048));

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;
        input.live.push_back(MakeLive(7, AssetType::KPAT_Model, "Sponza Runtime",
                                      ProductFullPath(ArchiveProductType::Model, kModelHash)));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Complete);
        EXPECT_TRUE(snapshot.diagnostics.empty());
        ASSERT_EQ(snapshot.nodes.size(), 1u);

        const AssetCatalogNode &node = snapshot.nodes[0];
        EXPECT_EQ(node.stable_key,
                  MakeArchiveProductCatalogKey(ArchiveProductType::Model, kModelHash));
        EXPECT_EQ(node.availability, AssetCatalogAvailability::LoadedArchiveProduct);
        EXPECT_EQ(node.dependency_coverage, AssetCatalogDependencyCoverage::Complete);
        ASSERT_TRUE(node.packed_runtime_asset_id.has_value());
        EXPECT_EQ(*node.packed_runtime_asset_id, AssetID(7, 1, AssetType::KPAT_Model).Pack());
        // Product identity fields survive the join.
        ASSERT_TRUE(node.content_hash.has_value());
        EXPECT_TRUE(*node.content_hash == kModelHash);
        EXPECT_EQ(node.byte_size, 2048u);
        EXPECT_FALSE(node.product_path.empty());
        // The archive had no readable name, so the runtime name is adopted.
        EXPECT_EQ(node.display_name, "Sponza Runtime");
    }

    TEST(AssetCatalogBuilderTest, ArchiveAuthoredNameOutranksTheRuntimeName)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Material, kStoneHash));

        ArchiveCatalogSource source = MakeSource("source/stone/stone.mtl", "stone.mtl");
        LinkProduct(source, ArchiveProductType::Material, kStoneHash, "Stone");
        archive.sources.push_back(std::move(source));

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;
        input.live.push_back(MakeLive(3, AssetType::KPAT_Material, "stone_runtime",
                                      ProductFullPath(ArchiveProductType::Material, kStoneHash)));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        ASSERT_EQ(snapshot.nodes.size(), 1u);
        const AssetCatalogNode &node = snapshot.nodes[0];
        EXPECT_EQ(node.availability, AssetCatalogAvailability::LoadedArchiveProduct);
        // Authored naming outranks the transient runtime name, which is kept
        // for search rather than discarded.
        EXPECT_EQ(node.display_name, "Stone");
        EXPECT_EQ(node.aliases, (std::vector<std::string>{"Stone", "stone_runtime"}));
    }

    TEST(AssetCatalogBuilderTest, PathMatchWithADifferentTypeStaysSeparateAndDiagnoses)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Material, kStoneHash));

        const std::string shared_path = ProductFullPath(ArchiveProductType::Material, kStoneHash);

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;
        input.live.push_back(MakeLive(5, AssetType::KPAT_Texture, "not_a_material", shared_path,
                                      /*path_indexed=*/true));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::InvalidArchiveLiveJoin),
                  1u);
        ASSERT_EQ(snapshot.nodes.size(), 2u);

        const AssetCatalogNode &archive_node = *FindNode(
            snapshot, MakeArchiveProductCatalogKey(ArchiveProductType::Material, kStoneHash));
        EXPECT_EQ(archive_node.availability, AssetCatalogAvailability::ArchiveOnly);
        EXPECT_FALSE(archive_node.packed_runtime_asset_id.has_value());

        const AssetCatalogNode &runtime_node = *FindNode(
            snapshot, MakeRuntimePathCatalogKey(AssetType::KPAT_Texture,
                                                CanonicalAssetPathKey(shared_path)));
        EXPECT_EQ(runtime_node.availability, AssetCatalogAvailability::RuntimeOnly);
        EXPECT_EQ(runtime_node.type, AssetType::KPAT_Texture);
        EXPECT_FALSE(runtime_node.archive_product_type.has_value());
        EXPECT_EQ(*runtime_node.packed_runtime_asset_id,
                  AssetID(5, 1, AssetType::KPAT_Texture).Pack());
    }

    TEST(AssetCatalogBuilderTest, TwoLiveRecordsSharingOneProductPathMergeOnlyOnce)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Model, kModelHash));

        const std::string shared_path = ProductFullPath(ArchiveProductType::Model, kModelHash);

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;
        // Inserted out of id order so the winner proves it is chosen by sort,
        // not by arrival.
        input.live.push_back(
            MakeLive(9, AssetType::KPAT_Model, "second", shared_path, /*path_indexed=*/true));
        input.live.push_back(
            MakeLive(3, AssetType::KPAT_Model, "first", shared_path, /*path_indexed=*/true));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        ASSERT_EQ(snapshot.nodes.size(), 2u);

        const AssetCatalogNode &merged = *FindNode(
            snapshot, MakeArchiveProductCatalogKey(ArchiveProductType::Model, kModelHash));
        EXPECT_EQ(merged.availability, AssetCatalogAvailability::LoadedArchiveProduct);
        EXPECT_EQ(*merged.packed_runtime_asset_id, AssetID(3, 1, AssetType::KPAT_Model).Pack());

        const AssetCatalogNode &other = *FindNode(
            snapshot, MakeRuntimePathCatalogKey(AssetType::KPAT_Model,
                                                CanonicalAssetPathKey(shared_path)));
        EXPECT_EQ(other.availability, AssetCatalogAvailability::RuntimeOnly);
        EXPECT_EQ(*other.packed_runtime_asset_id, AssetID(9, 1, AssetType::KPAT_Model).Pack());
    }

    TEST(AssetCatalogBuilderTest, PathIndexedAndIdentityStableKeysFollowTheContract)
    {
        const std::string level_path = AssetPath("level/sponza_test.level");
        const std::string mesh_path = AssetPath("mesh/hero.kpmesh");

        ModelArchiveCatalogSnapshot empty_archive;
        AssetCatalogBuildInput input = BaseInput();
        input.archive = &empty_archive;
        input.live.push_back(
            MakeLive(4, AssetType::KPAT_Level, "sponza_test", level_path, /*path_indexed=*/true));
        // A record the manager path index no longer resolves keeps an identity
        // key even though it still carries a path.
        input.live.push_back(MakeLive(11, AssetType::KPAT_Mesh, "hero", mesh_path));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Complete);
        ASSERT_EQ(snapshot.nodes.size(), 2u);

        const AssetCatalogNode &level = *FindNode(
            snapshot, MakeRuntimePathCatalogKey(AssetType::KPAT_Level,
                                                CanonicalAssetPathKey(level_path)));
        EXPECT_EQ(level.availability, AssetCatalogAvailability::RuntimeOnly);
        EXPECT_EQ(level.dependency_coverage, AssetCatalogDependencyCoverage::Complete);
        EXPECT_EQ(level.logical_path, "level/sponza_test.level");
        EXPECT_EQ(*level.packed_runtime_asset_id, AssetID(4, 1, AssetType::KPAT_Level).Pack());

        const AssetCatalogNode &mesh = *FindNode(
            snapshot, MakeRuntimeIdentityCatalogKey(AssetID(11, 1, AssetType::KPAT_Mesh)));
        EXPECT_EQ(mesh.availability, AssetCatalogAvailability::RuntimeOnly);
        EXPECT_EQ(mesh.logical_path, "mesh/hero.kpmesh");
        EXPECT_EQ(mesh.display_name, "hero");
        EXPECT_EQ(*mesh.packed_runtime_asset_id, AssetID(11, 1, AssetType::KPAT_Mesh).Pack());
    }

    TEST(AssetCatalogBuilderTest, OwnedChildrenEmitOneEdgeAndDependencyOrdinalsStayDense)
    {
        const std::string parent_path = AssetPath("level/root.level");
        const AssetID mesh = AssetID(2, 1, AssetType::KPAT_Mesh);
        const AssetID texture = AssetID(3, 1, AssetType::KPAT_Texture);
        const AssetID material = AssetID(4, 1, AssetType::KPAT_Material);

        AssetCatalogBuildInput input = BaseInput();
        LiveCatalogRecord parent =
            MakeLive(1, AssetType::KPAT_Level, "root", parent_path, /*path_indexed=*/true);
        parent.dependencies = {mesh, texture, material};
        parent.owned_children = {mesh};
        input.live.push_back(std::move(parent));
        input.live.push_back(MakeLive(2, AssetType::KPAT_Mesh, "root_mesh", {}));
        input.live.push_back(MakeLive(3, AssetType::KPAT_Texture, "root_atlas", {}));
        input.live.push_back(MakeLive(4, AssetType::KPAT_Material, "root_material", {}));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        ASSERT_EQ(snapshot.nodes.size(), 4u);
        const std::string parent_key =
            MakeRuntimePathCatalogKey(AssetType::KPAT_Level,
                                      CanonicalAssetPathKey(parent_path));
        ASSERT_NE(FindNode(snapshot, parent_key), nullptr);

        const std::uint32_t mesh_index =
            NodeIndexOf(snapshot, MakeRuntimeIdentityCatalogKey(mesh));
        const std::uint32_t texture_index =
            NodeIndexOf(snapshot, MakeRuntimeIdentityCatalogKey(texture));
        const std::uint32_t material_index =
            NodeIndexOf(snapshot, MakeRuntimeIdentityCatalogKey(material));
        ASSERT_NE(mesh_index, kNoNode);
        ASSERT_NE(texture_index, kNoNode);
        ASSERT_NE(material_index, kNoNode);

        const std::vector<const AssetCatalogEdge *> edges = EdgesFrom(snapshot, parent_key);
        ASSERT_EQ(edges.size(), 3u);

        std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> observed;
        for (const AssetCatalogEdge *edge : edges)
        {
            observed.emplace_back(edge->to.value, static_cast<std::uint32_t>(edge->relation),
                                  edge->ordinal);
        }
        std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> expected{
            {mesh_index, static_cast<std::uint32_t>(AssetCatalogRelation::OwnedChild), 0},
            {texture_index, static_cast<std::uint32_t>(AssetCatalogRelation::Dependency), 0},
            {material_index, static_cast<std::uint32_t>(AssetCatalogRelation::Dependency), 1}};
        std::sort(observed.begin(), observed.end());
        std::sort(expected.begin(), expected.end());
        EXPECT_EQ(observed, expected);
    }

    TEST(AssetCatalogBuilderTest, OwnedChildOutsideTheDependencyVectorIsDiagnosedAndKept)
    {
        AssetCatalogBuildInput input = BaseInput();
        LiveCatalogRecord parent = MakeLive(1, AssetType::KPAT_Level, "root",
                                            AssetPath("level/root.level"), /*path_indexed=*/true);
        parent.owned_children = {AssetID(2, 1, AssetType::KPAT_Mesh)};
        input.live.push_back(std::move(parent));
        input.live.push_back(MakeLive(2, AssetType::KPAT_Mesh, "orphan", {}));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::InvalidOwnedChildLayout),
                  1u);
        ASSERT_EQ(snapshot.edges.size(), 1u);
        EXPECT_EQ(snapshot.edges[0].relation, AssetCatalogRelation::OwnedChild);
        EXPECT_EQ(snapshot.edges[0].ordinal, 0u);
    }

    TEST(AssetCatalogBuilderTest, SelfOwnedChildIsDroppedRatherThanInvalidatingTheSnapshot)
    {
        AssetCatalogBuildInput input = BaseInput();
        LiveCatalogRecord parent = MakeLive(1, AssetType::KPAT_Level, "root",
                                            AssetPath("level/root.level"), /*path_indexed=*/true);
        parent.dependencies = {AssetID(1, 1, AssetType::KPAT_Level)};
        parent.owned_children = {AssetID(1, 1, AssetType::KPAT_Level)};
        input.live.push_back(std::move(parent));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::InvalidOwnedChildLayout),
                  1u);
        EXPECT_TRUE(snapshot.edges.empty());
        ASSERT_EQ(snapshot.nodes.size(), 1u);
    }

    TEST(AssetCatalogBuilderTest, SharedRepeatedAndCyclicReferencesArePreserved)
    {
        const AssetID level_id = AssetID(1, 1, AssetType::KPAT_Level);
        const AssetID shared = AssetID(9, 1, AssetType::KPAT_Texture);

        ModelArchiveCatalogSnapshot empty_archive;
        AssetCatalogBuildInput input = BaseInput();
        input.archive = &empty_archive;
        LiveCatalogRecord level = MakeLive(1, AssetType::KPAT_Level, "root", {});
        level.dependencies = {shared, shared};
        LiveCatalogRecord material = MakeLive(2, AssetType::KPAT_Material, "stone", {});
        material.dependencies = {shared};
        LiveCatalogRecord texture = MakeLive(9, AssetType::KPAT_Texture, "atlas", {});
        // A reference back to the level closes an intentional cycle.
        texture.dependencies = {level_id};
        input.live.push_back(std::move(level));
        input.live.push_back(std::move(material));
        input.live.push_back(std::move(texture));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Complete);
        ASSERT_EQ(snapshot.nodes.size(), 3u);
        ASSERT_EQ(snapshot.edges.size(), 4u);

        const std::string level_key = MakeRuntimeIdentityCatalogKey(level_id);
        const std::string material_key =
            MakeRuntimeIdentityCatalogKey(AssetID(2, 1, AssetType::KPAT_Material));
        const std::string texture_key = MakeRuntimeIdentityCatalogKey(shared);
        const std::uint32_t level_index = NodeIndexOf(snapshot, level_key);
        const std::uint32_t texture_index = NodeIndexOf(snapshot, texture_key);
        ASSERT_NE(level_index, kNoNode);
        ASSERT_NE(texture_index, kNoNode);

        // Both repeated references survive as distinct dependency ordinals, and
        // neither becomes an owned child.
        std::vector<std::uint32_t> level_ordinals;
        for (const AssetCatalogEdge *edge : EdgesFrom(snapshot, level_key))
        {
            EXPECT_EQ(edge->to.value, texture_index);
            EXPECT_EQ(edge->relation, AssetCatalogRelation::Dependency);
            level_ordinals.push_back(edge->ordinal);
        }
        std::sort(level_ordinals.begin(), level_ordinals.end());
        EXPECT_EQ(level_ordinals, (std::vector<std::uint32_t>{0u, 1u}));

        // The shared target keeps one incoming edge per reference site, and the
        // cycle back to the level is preserved.
        std::size_t incoming = 0;
        bool cycle_present = false;
        for (const AssetCatalogEdge &edge : snapshot.edges)
        {
            if (edge.to.value == texture_index)
            {
                ++incoming;
            }
            if (edge.from.value == texture_index && edge.to.value == level_index)
            {
                cycle_present = true;
            }
        }
        EXPECT_EQ(incoming, 3u);
        EXPECT_TRUE(cycle_present);
        EXPECT_EQ(EdgesFrom(snapshot, material_key).size(), 1u);
    }

    TEST(AssetCatalogBuilderTest, MissingLiveEndpointsSynthesizeDeterministicReferenceNodes)
    {
        AssetCatalogBuildInput input = BaseInput();
        LiveCatalogRecord owner = MakeLive(1, AssetType::KPAT_Model, "root", {});
        owner.dependencies = {AssetID(100, 1, AssetType::KPAT_Mesh),
                              AssetID(200, 1, AssetType::KPAT_Texture)};
        LiveCatalogRecord other = MakeLive(2, AssetType::KPAT_Model, "other", {});
        // Same absent target, different owner: a separate node, not a merge.
        other.dependencies = {AssetID(100, 1, AssetType::KPAT_Mesh)};
        input.live.push_back(std::move(owner));
        input.live.push_back(std::move(other));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        // Two live nodes plus three synthesized endpoints.
        ASSERT_EQ(snapshot.nodes.size(), 5u);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::UnresolvedDependency),
                  3u);

        const std::string owner_key =
            MakeRuntimeIdentityCatalogKey(AssetID(1, 1, AssetType::KPAT_Model));
        const std::string other_key =
            MakeRuntimeIdentityCatalogKey(AssetID(2, 1, AssetType::KPAT_Model));

        const AssetCatalogNode *mesh_missing = FindNode(
            snapshot, MakeMissingReferenceCatalogKey(owner_key,
                                                     AssetCatalogRelation::Dependency, 0,
                                                     AssetType::KPAT_Mesh));
        ASSERT_NE(mesh_missing, nullptr);
        EXPECT_EQ(mesh_missing->kind, AssetCatalogNodeKind::MissingReference);
        EXPECT_EQ(mesh_missing->type, AssetType::KPAT_Mesh);
        EXPECT_EQ(mesh_missing->type_name, "Mesh");
        EXPECT_EQ(mesh_missing->display_name, "Missing Mesh");
        EXPECT_EQ(mesh_missing->availability, AssetCatalogAvailability::Missing);
        EXPECT_EQ(mesh_missing->dependency_coverage, AssetCatalogDependencyCoverage::Unknown);
        EXPECT_FALSE(mesh_missing->archive_product_type.has_value());
        EXPECT_FALSE(mesh_missing->content_hash.has_value());
        EXPECT_FALSE(mesh_missing->packed_runtime_asset_id.has_value());

        const AssetCatalogNode *texture_missing = FindNode(
            snapshot, MakeMissingReferenceCatalogKey(owner_key,
                                                     AssetCatalogRelation::Dependency, 1,
                                                     AssetType::KPAT_Texture));
        ASSERT_NE(texture_missing, nullptr);
        EXPECT_EQ(texture_missing->display_name, "Missing Texture");

        const AssetCatalogNode *other_missing = FindNode(
            snapshot, MakeMissingReferenceCatalogKey(other_key,
                                                     AssetCatalogRelation::Dependency, 0,
                                                     AssetType::KPAT_Mesh));
        ASSERT_NE(other_missing, nullptr);

        // The unresolved diagnostic is contract-required for each node, and a
        // missing target is never the source of an edge.
        for (const AssetCatalogNode *node : {mesh_missing, texture_missing, other_missing})
        {
            const bool has_diagnostic = std::any_of(
                snapshot.diagnostics.begin(), snapshot.diagnostics.end(),
                [node](const AssetCatalogDiagnostic &entry)
                {
                    return entry.code == AssetCatalogDiagnosticCode::UnresolvedDependency &&
                           entry.related_stable_key == node->stable_key;
                });
            EXPECT_TRUE(has_diagnostic) << node->stable_key;
            EXPECT_TRUE(EdgesFrom(snapshot, node->stable_key).empty());
        }

        // The same input always assembles the same catalog.
        EXPECT_TRUE(SameBytes(snapshot, BuildAssetCatalog(input)));
    }

    TEST(AssetCatalogBuilderTest, UnknownTypeDescriptorsUseHexadecimalFallbackNames)
    {
        constexpr AssetType kCustomType = static_cast<AssetType>(0x1234);

        AssetCatalogBuildInput input = BaseInput();
        LiveCatalogRecord custom = MakeLive(6, kCustomType, "custom_thing", {});
        custom.dependencies = {AssetID(77, 1, kCustomType)};
        input.live.push_back(std::move(custom));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::UnknownTypeDescriptor),
                  1u);
        ASSERT_EQ(snapshot.nodes.size(), 2u);

        const AssetCatalogNode &node =
            *FindNode(snapshot, MakeRuntimeIdentityCatalogKey(AssetID(6, 1, kCustomType)));
        EXPECT_EQ(node.type, kCustomType);
        EXPECT_EQ(node.type_name, "AssetType 0x1234");
        EXPECT_EQ(node.display_name, "custom_thing");

        // The synthesized endpoint uses the same switch-free name.
        const AssetCatalogNode &missing = *FindNode(
            snapshot, MakeMissingReferenceCatalogKey(node.stable_key,
                                                     AssetCatalogRelation::Dependency, 0,
                                                     kCustomType));
        EXPECT_EQ(missing.type_name, "AssetType 0x1234");
        EXPECT_EQ(missing.display_name, "Missing AssetType 0x1234");
    }

    TEST(AssetCatalogBuilderTest, MissingArchiveStillPublishesTheLiveGraph)
    {
        AssetCatalogBuildInput input = BaseInput();
        input.archive = nullptr;
        input.archive_diagnostic = "archive catalog read failed for 'x': file is not a database";
        input.archive_diagnostic_code = AssetCatalogDiagnosticCode::ArchiveUnavailable;
        input.archive_diagnostic_severity = AssetCatalogDiagnosticSeverity::Error;
        input.live.push_back(MakeLive(1, AssetType::KPAT_Level, "root",
                                      AssetPath("level/root.level"), /*path_indexed=*/true));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        ASSERT_EQ(snapshot.nodes.size(), 1u);
        EXPECT_EQ(snapshot.nodes[0].availability, AssetCatalogAvailability::RuntimeOnly);
        ASSERT_EQ(snapshot.diagnostics.size(), 1u);
        EXPECT_EQ(snapshot.diagnostics[0].code, AssetCatalogDiagnosticCode::ArchiveUnavailable);
        EXPECT_EQ(snapshot.diagnostics[0].severity, AssetCatalogDiagnosticSeverity::Error);
        EXPECT_EQ(snapshot.diagnostics[0].message, input.archive_diagnostic);
    }

    TEST(AssetCatalogBuilderTest, FailedSourcesWarnAndKeepTheirProducts)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Model, kModelHash));
        ArchiveCatalogSource failed = MakeSource("source/broken/broken.obj", "broken.obj",
                                                 SourceImportStatus::Failed);
        failed.source.diagnostic = "unsupported node 0x42";
        LinkProduct(failed, ArchiveProductType::Model, kModelHash, "broken_model");
        archive.sources.push_back(std::move(failed));

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::ArchiveSourceFailed), 1u);
        // The product the failed source still published remains cataloged.
        ASSERT_EQ(snapshot.nodes.size(), 1u);
        EXPECT_EQ(snapshot.nodes[0].availability, AssetCatalogAvailability::ArchiveOnly);
        EXPECT_EQ(snapshot.nodes[0].display_name, "broken_model");
    }

    TEST(AssetCatalogBuilderTest, NodeLimitDropsTheLivePortionAndKeepsTheArchivePortion)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Model, kModelHash));

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;
        input.limits.max_nodes = 1;
        input.live.push_back(MakeLive(1, AssetType::KPAT_Level, "one", {}));
        input.live.push_back(MakeLive(2, AssetType::KPAT_Mesh, "two", {}));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::CaptureLimitExceeded), 1u);
        ASSERT_EQ(snapshot.nodes.size(), 1u);
        EXPECT_EQ(snapshot.nodes[0].availability, AssetCatalogAvailability::ArchiveOnly);
        EXPECT_TRUE(snapshot.edges.empty());
    }

    TEST(AssetCatalogBuilderTest, ArchiveOverTheNodeLimitPublishesNoArchivePortion)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Model, kModelHash));
        archive.products.push_back(MakeProduct(ArchiveProductType::Material, kStoneHash));

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;
        input.limits.max_nodes = 1;
        input.live.push_back(MakeLive(1, AssetType::KPAT_Level, "root", {}));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::CaptureLimitExceeded), 1u);
        // All or nothing: no partial archive selection reaches the caller.
        ASSERT_EQ(snapshot.nodes.size(), 1u);
        EXPECT_EQ(snapshot.nodes[0].availability, AssetCatalogAvailability::RuntimeOnly);
    }

    TEST(AssetCatalogBuilderTest, LiveLimitDiagnosticDropsTheLivePortion)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Model, kModelHash));

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;
        input.live.push_back(MakeLive(1, AssetType::KPAT_Level, "root", {}));
        input.live_limit_diagnostic = "the live asset graph holds more than 1 assets";

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        ASSERT_EQ(snapshot.nodes.size(), 1u);
        EXPECT_EQ(snapshot.nodes[0].availability, AssetCatalogAvailability::ArchiveOnly);
        ASSERT_EQ(snapshot.diagnostics.size(), 1u);
        EXPECT_EQ(snapshot.diagnostics[0].code, AssetCatalogDiagnosticCode::CaptureLimitExceeded);
        EXPECT_EQ(snapshot.diagnostics[0].severity, AssetCatalogDiagnosticSeverity::Error);
        EXPECT_EQ(snapshot.diagnostics[0].message, input.live_limit_diagnostic);
    }

    TEST(AssetCatalogBuilderTest, EdgeLimitStopsEmissionDeterministically)
    {
        AssetCatalogBuildInput input = BaseInput();
        input.limits.max_edges = 2;
        LiveCatalogRecord parent = MakeLive(1, AssetType::KPAT_Level, "root", {});
        parent.dependencies = {AssetID(2, 1, AssetType::KPAT_Mesh),
                               AssetID(3, 1, AssetType::KPAT_Texture),
                               AssetID(4, 1, AssetType::KPAT_Material)};
        input.live.push_back(std::move(parent));
        input.live.push_back(MakeLive(2, AssetType::KPAT_Mesh, "mesh", {}));
        input.live.push_back(MakeLive(3, AssetType::KPAT_Texture, "texture", {}));
        input.live.push_back(MakeLive(4, AssetType::KPAT_Material, "material", {}));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::CaptureLimitExceeded), 1u);
        // The first two dependency ordinals survive; the third edge is never
        // emitted, and no endpoint is synthesized for it either.
        ASSERT_EQ(snapshot.edges.size(), 2u);
        EXPECT_EQ(snapshot.edges[0].ordinal, 0u);
        EXPECT_EQ(snapshot.edges[1].ordinal, 1u);
        EXPECT_EQ(snapshot.nodes.size(), 4u);
    }

    TEST(AssetCatalogBuilderTest, AliasLimitTruncatesToTheSmallestNames)
    {
        ModelArchiveCatalogSnapshot archive;
        archive.products.push_back(MakeProduct(ArchiveProductType::Model, kModelHash));
        for (const std::string name : {"alias_d", "alias_c", "alias_b", "alias_a"})
        {
            ArchiveCatalogSource source =
                MakeSource("source/" + name + "/" + name + ".obj", name + ".obj");
            LinkProduct(source, ArchiveProductType::Model, kModelHash, name);
            archive.sources.push_back(std::move(source));
        }

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;
        input.limits.max_aliases_per_node = 2;

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::CaptureLimitExceeded), 1u);
        ASSERT_EQ(snapshot.nodes.size(), 1u);
        EXPECT_EQ(snapshot.nodes[0].aliases, (std::vector<std::string>{"alias_a", "alias_b"}));
        EXPECT_EQ(snapshot.nodes[0].display_name, "alias_a");
        // Provenance is metadata, not an alias, so it is not truncated.
        EXPECT_EQ(snapshot.nodes[0].provenance.size(), 4u);
    }

    TEST(AssetCatalogBuilderTest, DiagnosticLimitKeepsRequiredEntriesAndTheNotice)
    {
        ModelArchiveCatalogSnapshot archive;
        for (int index = 0; index < 5; ++index)
        {
            const std::string name = "failed_" + std::to_string(index);
            archive.sources.push_back(MakeSource("source/" + name + "/" + name + ".obj",
                                                 name + ".obj", SourceImportStatus::Failed));
        }

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;
        input.limits.max_diagnostics = 6;
        LiveCatalogRecord owner = MakeLive(1, AssetType::KPAT_Model, "root", {});
        owner.dependencies = {AssetID(100, 1, AssetType::KPAT_Mesh),
                              AssetID(200, 1, AssetType::KPAT_Texture)};
        input.live.push_back(std::move(owner));

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        // Both contract-required entries survive, three optional warnings fill
        // the rest, and the reserved slot explains the truncation.
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::UnresolvedDependency), 2u);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::ArchiveSourceFailed), 3u);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::CaptureLimitExceeded), 1u);
        EXPECT_EQ(snapshot.diagnostics.size(), 6u);
    }

    TEST(AssetCatalogBuilderTest, DiagnosticLimitNeverDropsRequiredEntries)
    {
        ModelArchiveCatalogSnapshot archive;
        for (int index = 0; index < 4; ++index)
        {
            const std::string name = "failed_" + std::to_string(index);
            archive.sources.push_back(MakeSource("source/" + name + "/" + name + ".obj",
                                                 name + ".obj", SourceImportStatus::Failed));
        }

        AssetCatalogBuildInput input = BaseInput();
        input.archive = &archive;
        input.limits.max_diagnostics = 2;
        for (std::uint32_t index = 0; index < 3; ++index)
        {
            LiveCatalogRecord owner =
                MakeLive(index + 1, AssetType::KPAT_Model, "owner_" + std::to_string(index), {});
            owner.dependencies = {AssetID(100 + index, 1, AssetType::KPAT_Mesh)};
            input.live.push_back(std::move(owner));
        }

        const AssetCatalogSnapshot snapshot = BuildAssetCatalog(input);

        ExpectValid(snapshot);
        EXPECT_EQ(snapshot.status, AssetCatalogSnapshotStatus::Partial);
        // A missing-reference node is invalid without its diagnostic, so the
        // required set exceeds the configured budget instead of breaking the
        // contract. No optional entry and no notice fit.
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::UnresolvedDependency), 3u);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::ArchiveSourceFailed), 0u);
        EXPECT_EQ(CountDiagnostics(snapshot, AssetCatalogDiagnosticCode::CaptureLimitExceeded), 0u);
        EXPECT_EQ(snapshot.diagnostics.size(), 3u);
    }

    TEST(AssetCatalogBuilderTest, DifferentRawInsertionOrdersCanonicalizeIdentically)
    {
        const auto fill = [](ModelArchiveCatalogSnapshot &archive, AssetCatalogBuildInput &input,
                             bool reverse)
        {
            ArchiveCatalogSource sponza = MakeSource("source/sponza/sponza.obj", "sponza.obj");
            LinkProduct(sponza, ArchiveProductType::Model, kModelHash, "Sponza");
            sponza.dependencies.push_back({"source/sponza/sponza.mtl", {}, 0, 0});
            ArchiveCatalogSource stone = MakeSource("source/stone/stone.mtl", "stone.mtl");
            LinkProduct(stone, ArchiveProductType::Material, kStoneHash, "stone", 0);
            ArchiveCatalogSource failed = MakeSource("source/broken/broken.obj", "broken.obj",
                                                     SourceImportStatus::Failed);
            archive.products = {MakeProduct(ArchiveProductType::Material, kStoneHash),
                                MakeProduct(ArchiveProductType::Model, kModelHash)};
            archive.sources = {std::move(sponza), std::move(stone), std::move(failed)};

            LiveCatalogRecord level = MakeLive(1, AssetType::KPAT_Level, "root",
                                               AssetPath("level/root.level"),
                                               /*path_indexed=*/true);
            level.dependencies = {AssetID(2, 1, AssetType::KPAT_Model),
                                  AssetID(9, 1, AssetType::KPAT_Mesh)};
            level.owned_children = {AssetID(9, 1, AssetType::KPAT_Mesh)};
            LiveCatalogRecord model =
                MakeLive(2, AssetType::KPAT_Model, "Sponza",
                         ProductFullPath(ArchiveProductType::Model, kModelHash),
                         /*path_indexed=*/true);

            input = BaseInput();
            input.archive = &archive;
            input.live = {std::move(level), std::move(model)};
            if (reverse)
            {
                std::reverse(archive.products.begin(), archive.products.end());
                std::reverse(archive.sources.begin(), archive.sources.end());
                std::reverse(input.live.begin(), input.live.end());
                std::reverse(input.type_names.begin(), input.type_names.end());
            }
        };

        ModelArchiveCatalogSnapshot forward_archive;
        AssetCatalogBuildInput forward_input;
        fill(forward_archive, forward_input, false);
        const AssetCatalogSnapshot first = BuildAssetCatalog(forward_input);

        ModelArchiveCatalogSnapshot backward_archive;
        AssetCatalogBuildInput backward_input;
        fill(backward_archive, backward_input, true);
        const AssetCatalogSnapshot second = BuildAssetCatalog(backward_input);

        ExpectValid(first);
        ExpectValid(second);
        EXPECT_EQ(first.revision, second.revision);
        EXPECT_TRUE(SameBytes(first, second));
        // Both orders must produce a non-trivial graph, so the comparison above
        // is not vacuously true over empty snapshots.
        ASSERT_EQ(first.nodes.size(), 4u);
        EXPECT_EQ(first.status, AssetCatalogSnapshotStatus::Partial);
    }
}
