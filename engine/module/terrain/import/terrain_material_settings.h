#ifndef KPENGINE_TERRAIN_IMPORT_TERRAIN_MATERIAL_SETTINGS_H
#define KPENGINE_TERRAIN_IMPORT_TERRAIN_MATERIAL_SETTINGS_H

#include <filesystem>
#include <string>
#include <vector>

#include "asset/imported_model.h"
#include "asset/common.h"
#include "product/terrain_core.h"

namespace kpengine::terrain
{
    enum class TerrainMaterialMaskKind
    {
        HeightBand,
        SlopeOverlay,
    };

    struct TerrainMaterialLayerMask final
    {
        TerrainMaterialMaskKind kind{TerrainMaterialMaskKind::HeightBand};
        float height_max{};
        float blend_width{};
        float slope_min_radians{};
        float slope_blend_width_radians{};
        float height_min{};
        float height_max_for_overlay{1.0f};
        float height_fade_width{};
    };

    struct TerrainMaterialSettings final
    {
        std::filesystem::path settings_path;
        asset::ImportedModelDocument document;
        float texture_tile_size_m{8.0f};
        std::vector<TerrainMaterialLayerMask> layer_masks;
        std::vector<std::string> layer_ids;
        std::vector<std::string> names;
        std::vector<std::filesystem::path> source_files;
    };

    bool LoadTerrainMaterialSettings(const std::filesystem::path &settings_path,
                                     const std::filesystem::path &asset_root,
                                     TerrainMaterialSettings &settings,
                                     std::string &diagnostic);

    void BuildTerrainLayerMaterial(const TerrainMaterialSettings &settings,
                                   const ScalarField2D &heightfield,
                                   asset::ImportedModelDocument &document);
    void AssignTerrainMaterialSections(data::MeshData &mesh);

    bool RegisterTerrainPreviewMaterials(const TerrainMaterialSettings &settings,
                                        const ScalarField2D &heightfield,
                                        const std::filesystem::path &asset_root,
                                        const std::filesystem::path &archive_root,
                                        std::vector<asset::AssetID> &material_ids,
                                        std::vector<asset::AssetID> &texture_ids,
                                        std::string &diagnostic);
}

#endif
