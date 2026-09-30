#ifndef KPENGINE_TERRAIN_IMPORT_TERRAIN_BAKER_H
#define KPENGINE_TERRAIN_IMPORT_TERRAIN_BAKER_H

#include <memory>
#include <string>
#include <cstddef>

#include "evaluation/terrain_generation.h"

namespace kpengine::terrain
{
    struct TerrainBakeResult
    {
        bool succeeded = false;
        std::string logical_model_path;
        std::string provenance_path;
        std::string diagnostic;
        double bake_time_ms = 0.0;
        std::size_t model_bytes = 0;
        std::size_t material_bytes = 0;
    };

    class TerrainBaker final
    {
    public:
        static TerrainBakeResult Bake(const TerrainRecipe &recipe,
                                      const ScalarField2D &heightfield);
    };
}

#endif
