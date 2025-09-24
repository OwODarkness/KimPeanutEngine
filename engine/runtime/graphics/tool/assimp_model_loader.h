#ifndef KPENGINE_RUNTIME_GRAPHICS_ASSMIP_MODEL_LOADER_H
#define KPENGINE_RUNTIME_GRAPHICS_ASSMIP_MODEL_LOADER_H

#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <unordered_map>
#include "model_loader.h"

namespace kpengine::graphics{
    class AssimpModelLoader: public IModelLoader{
    public:
        bool Load(const std::string& path, MeshResource& mesh) override;
    private:
        void ProcessNode(aiNode* node, const aiScene* scene, MeshResource& resource, std::unordered_map<Vertex, uint32_t, VertexHash>& unique_vertices);
        void ProcessMesh(aiMesh* mesh, const aiScene* scene, MeshResource& resource, std::unordered_map<Vertex, uint32_t, VertexHash>& unique_vertices);
    };
}

#endif