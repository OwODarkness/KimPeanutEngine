#ifndef KPENGINE_TERRAIN_EVALUATION_TERRAIN_HYDRAULIC_EROSION_H
#define KPENGINE_TERRAIN_EVALUATION_TERRAIN_HYDRAULIC_EROSION_H

#include <string>

namespace kpengine::terrain
{
    class OperatorRegistry;

    bool RegisterTerrainHydraulicOperators(OperatorRegistry &registry,
                                           std::string &diagnostic);
}

#endif
