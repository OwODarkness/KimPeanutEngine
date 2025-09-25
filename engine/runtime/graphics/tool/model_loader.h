#ifndef KPENGINE_RUNTIME_GRAPHICS_MODEL_LOADER_H
#define KPENGINE_RUNTIME_GRAPHICS_MODEL_LOADER_H

#include <string>
#include "common/mesh_data.h"

namespace kpengine::graphics{
    class IModelLoader{
    public:
        virtual bool Load(const std::string& path, MeshData& resource) = 0;
    };
}

#endif