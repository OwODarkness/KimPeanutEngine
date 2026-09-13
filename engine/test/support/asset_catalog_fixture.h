#ifndef KPENGINE_TEST_SUPPORT_ASSET_CATALOG_FIXTURE_H
#define KPENGINE_TEST_SUPPORT_ASSET_CATALOG_FIXTURE_H

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asset/asset_catalog.h"

namespace kpengine::asset::test
{
    // A Sponza-shaped catalog graph with one shared texture, one unresolved
    // reference, and one deliberate cycle. It is value-only so Asset and Editor
    // tests can consume the same graph without SQLite, files, AssetManager, or
    // ImGui.
    //
    //   Level: level/sponza_test.level [RuntimeOnly]
    //     Dependency[0] -> Model: model/sponza [LoadedArchiveProduct]
    //                        OwnedChild[0] -> Mesh [RuntimeOnly]
    //                        Dependency[0] -> Material: stone
    //                        Dependency[1] -> Material: fabric
    //     Material: stone -> Texture: shared_albedo
    //     Material: fabric -> Texture: shared_albedo   (shared node)
    //                      -> Texture: fabric_normal   (ArchiveOnly)
    //                      -> Missing: fabric_orm      (unresolved)
    //                      -> Model: model/sponza      (intentional cycle)
    inline AssetCatalogSnapshot MakeAssetCatalogContractFixture()
    {
        const ContentHash model_hash = Sha256("kpengine.asset-catalog.fixture/model/sponza");
        const ContentHash stone_hash = Sha256("kpengine.asset-catalog.fixture/material/stone");
        const ContentHash fabric_hash = Sha256("kpengine.asset-catalog.fixture/material/fabric");
        const ContentHash albedo_hash = Sha256("kpengine.asset-catalog.fixture/texture/shared_albedo");
        const ContentHash normal_hash = Sha256("kpengine.asset-catalog.fixture/texture/fabric_normal");

        const auto product_path = [](std::string_view folder, std::string_view extension,
                                     const ContentHash &hash)
        {
            return "products/" + std::string(folder) + "/" + hash.ToHex() +
                   std::string(extension);
        };

        // Node ids are assigned in a deliberately non-canonical insertion order
        // so that callers must go through canonicalization to get dense,
        // stable-key-ordered ids.
        AssetCatalogSnapshot snapshot;
        snapshot.revision = 1;
        snapshot.status = AssetCatalogSnapshotStatus::Partial;
        snapshot.nodes.resize(8);

        const auto init = [&snapshot](std::uint32_t index, std::string key,
                                      std::string type_name, std::string display_name)
        {
            AssetCatalogNode &node = snapshot.nodes[index];
            node.id.value = index;
            node.stable_key = std::move(key);
            node.type_name = std::move(type_name);
            node.display_name = std::move(display_name);
        };

        const std::string fabric_key =
            MakeArchiveProductCatalogKey(ArchiveProductType::Material, fabric_hash);
        const std::uint32_t fabric = 0;
        const std::uint32_t shared_albedo = 1;
        const std::uint32_t level = 2;
        const std::uint32_t fabric_normal = 3;
        const std::uint32_t model = 4;
        const std::uint32_t mesh = 5;
        const std::uint32_t stone = 6;
        const std::uint32_t missing_orm = 7;

        init(fabric, fabric_key, "Material", "fabric");
        snapshot.nodes[fabric].type = AssetType::KPAT_Material;
        snapshot.nodes[fabric].availability = AssetCatalogAvailability::LoadedArchiveProduct;
        snapshot.nodes[fabric].dependency_coverage = AssetCatalogDependencyCoverage::Complete;
        snapshot.nodes[fabric].archive_product_type = ArchiveProductType::Material;
        snapshot.nodes[fabric].content_hash = fabric_hash;
        snapshot.nodes[fabric].product_path =
            product_path("material", ".kpmaterial", fabric_hash);
        snapshot.nodes[fabric].packed_runtime_asset_id =
            AssetID(2, 1, AssetType::KPAT_Material).Pack();
        snapshot.nodes[fabric].byte_size = 3072;
        snapshot.nodes[fabric].schema_version = 1;

        init(shared_albedo,
             MakeArchiveProductCatalogKey(ArchiveProductType::Texture, albedo_hash),
             "Texture", "shared_albedo");
        snapshot.nodes[shared_albedo].type = AssetType::KPAT_Texture;
        snapshot.nodes[shared_albedo].availability =
            AssetCatalogAvailability::LoadedArchiveProduct;
        snapshot.nodes[shared_albedo].dependency_coverage =
            AssetCatalogDependencyCoverage::Complete;
        snapshot.nodes[shared_albedo].archive_product_type = ArchiveProductType::Texture;
        snapshot.nodes[shared_albedo].content_hash = albedo_hash;
        snapshot.nodes[shared_albedo].product_path =
            product_path("texture", ".kptexture", albedo_hash);
        snapshot.nodes[shared_albedo].packed_runtime_asset_id =
            AssetID(3, 1, AssetType::KPAT_Texture).Pack();
        snapshot.nodes[shared_albedo].aliases = {"texture/shared_albedo", "albedo"};
        snapshot.nodes[shared_albedo].byte_size = 1468006;
        snapshot.nodes[shared_albedo].schema_version = 1;

        init(level, MakeRuntimePathCatalogKey(AssetType::KPAT_Level, "level/sponza_test.level"),
             "Level", "sponza_test.level");
        snapshot.nodes[level].type = AssetType::KPAT_Level;
        snapshot.nodes[level].availability = AssetCatalogAvailability::RuntimeOnly;
        snapshot.nodes[level].dependency_coverage = AssetCatalogDependencyCoverage::Complete;
        snapshot.nodes[level].logical_path = "level/sponza_test.level";
        snapshot.nodes[level].packed_runtime_asset_id =
            AssetID(4, 1, AssetType::KPAT_Level).Pack();
        snapshot.nodes[level].byte_size = 512;
        snapshot.nodes[level].schema_version = 1;

        init(fabric_normal,
             MakeArchiveProductCatalogKey(ArchiveProductType::Texture, normal_hash),
             "Texture", "fabric_normal");
        snapshot.nodes[fabric_normal].type = AssetType::KPAT_Texture;
        snapshot.nodes[fabric_normal].availability = AssetCatalogAvailability::ArchiveOnly;
        snapshot.nodes[fabric_normal].dependency_coverage =
            AssetCatalogDependencyCoverage::Unknown;
        snapshot.nodes[fabric_normal].archive_product_type = ArchiveProductType::Texture;
        snapshot.nodes[fabric_normal].content_hash = normal_hash;
        snapshot.nodes[fabric_normal].product_path =
            product_path("texture", ".kptexture", normal_hash);
        snapshot.nodes[fabric_normal].byte_size = 524288;
        snapshot.nodes[fabric_normal].schema_version = 1;

        init(model, MakeArchiveProductCatalogKey(ArchiveProductType::Model, model_hash),
             "Model", "Sponza");
        snapshot.nodes[model].type = AssetType::KPAT_Model;
        snapshot.nodes[model].availability = AssetCatalogAvailability::LoadedArchiveProduct;
        snapshot.nodes[model].dependency_coverage = AssetCatalogDependencyCoverage::Complete;
        snapshot.nodes[model].archive_product_type = ArchiveProductType::Model;
        snapshot.nodes[model].content_hash = model_hash;
        snapshot.nodes[model].product_path = product_path("model", ".kpmodel", model_hash);
        snapshot.nodes[model].packed_runtime_asset_id =
            AssetID(1, 1, AssetType::KPAT_Model).Pack();
        snapshot.nodes[model].byte_size = 8598323;
        snapshot.nodes[model].schema_version = 1;
        snapshot.nodes[model].logical_path = "model/sponza";
        snapshot.nodes[model].aliases = {"Sponza", "model/sponza"};
        // Import provenance is copied metadata and never becomes a graph edge.
        {
            AssetCatalogProvenance second;
            second.source_path = "source/sponza/sponza.obj";
            second.source_display_name = "sponza.obj";
            second.source_dependency_paths = {"source/sponza/sponza.mtl"};
            AssetCatalogProvenance first;
            first.source_path = "source/sponza/sponza.mtl";
            first.source_display_name = "sponza.mtl";
            first.material_overrides.push_back(AssetCatalogMaterialOverride{2, "material/stone"});
            first.material_overrides.push_back(AssetCatalogMaterialOverride{0, "material/fabric"});
            snapshot.nodes[model].provenance = {std::move(second), std::move(first)};
        }

        init(mesh, MakeRuntimeIdentityCatalogKey(AssetID(5, 1, AssetType::KPAT_Mesh)),
             "Mesh", "sponza_mesh");
        snapshot.nodes[mesh].type = AssetType::KPAT_Mesh;
        snapshot.nodes[mesh].availability = AssetCatalogAvailability::RuntimeOnly;
        snapshot.nodes[mesh].packed_runtime_asset_id =
            AssetID(5, 1, AssetType::KPAT_Mesh).Pack();
        snapshot.nodes[mesh].byte_size = 2097152;

        init(stone,
             MakeArchiveProductCatalogKey(ArchiveProductType::Material, stone_hash),
             "Material", "stone");
        snapshot.nodes[stone].type = AssetType::KPAT_Material;
        snapshot.nodes[stone].availability = AssetCatalogAvailability::LoadedArchiveProduct;
        snapshot.nodes[stone].dependency_coverage = AssetCatalogDependencyCoverage::Complete;
        snapshot.nodes[stone].archive_product_type = ArchiveProductType::Material;
        snapshot.nodes[stone].content_hash = stone_hash;
        snapshot.nodes[stone].product_path = product_path("material", ".kpmaterial", stone_hash);
        snapshot.nodes[stone].packed_runtime_asset_id =
            AssetID(6, 1, AssetType::KPAT_Material).Pack();
        snapshot.nodes[stone].byte_size = 3072;
        snapshot.nodes[stone].schema_version = 1;

        init(missing_orm,
             MakeMissingReferenceCatalogKey(fabric_key, AssetCatalogRelation::Dependency,
                                            2, AssetType::KPAT_Texture),
             "Texture", "fabric_orm");
        snapshot.nodes[missing_orm].kind = AssetCatalogNodeKind::MissingReference;
        snapshot.nodes[missing_orm].type = AssetType::KPAT_Texture;
        snapshot.nodes[missing_orm].availability = AssetCatalogAvailability::Missing;
        snapshot.nodes[missing_orm].dependency_coverage =
            AssetCatalogDependencyCoverage::Unknown;
        snapshot.nodes[missing_orm].logical_path = "texture/fabric_orm";
        snapshot.nodes[missing_orm].aliases = {"fabric_orm"};

        const auto add_edge = [&snapshot](std::uint32_t from, std::uint32_t to,
                                          AssetCatalogRelation relation,
                                          std::uint32_t ordinal, std::string label)
        {
            AssetCatalogEdge edge;
            edge.from.value = from;
            edge.to.value = to;
            edge.relation = relation;
            edge.ordinal = ordinal;
            edge.label = std::move(label);
            snapshot.edges.push_back(std::move(edge));
        };

        add_edge(fabric, shared_albedo, AssetCatalogRelation::Dependency, 0, "albedo");
        add_edge(model, mesh, AssetCatalogRelation::OwnedChild, 0, "mesh[0]");
        add_edge(level, model, AssetCatalogRelation::Dependency, 0, "model");
        add_edge(fabric, missing_orm, AssetCatalogRelation::Dependency, 2, "orm");
        add_edge(model, stone, AssetCatalogRelation::Dependency, 0, "material[0]");
        add_edge(stone, shared_albedo, AssetCatalogRelation::Dependency, 0, "albedo");
        add_edge(fabric, fabric_normal, AssetCatalogRelation::Dependency, 1, "normal");
        add_edge(model, fabric, AssetCatalogRelation::Dependency, 1, "material[1]");
        // Synthetic and deliberately malformed: a material referencing the model
        // that owns it. The catalog must describe hostile input, not repair it.
        add_edge(fabric, model, AssetCatalogRelation::Dependency, 3, "model");

        AssetCatalogDiagnostic unresolved;
        unresolved.severity = AssetCatalogDiagnosticSeverity::Warning;
        unresolved.code = AssetCatalogDiagnosticCode::UnresolvedDependency;
        unresolved.message = "authored texture reference is not archived";
        unresolved.related_stable_key = snapshot.nodes[missing_orm].stable_key;
        snapshot.diagnostics.push_back(std::move(unresolved));

        std::string diagnostic;
        const bool canonicalized = CanonicalizeAssetCatalogSnapshot(snapshot, diagnostic);
        assert(canonicalized && "the contract fixture must canonicalize");
        (void)canonicalized;
        (void)diagnostic;
        return snapshot;
    }
}

#endif
