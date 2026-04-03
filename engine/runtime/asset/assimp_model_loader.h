#ifndef KPENGINE_RUNTIME_ASSET_ASSIMP_MODEL_LOADER_H
#define KPENGINE_RUNTIME_ASSET_ASSIMP_MODEL_LOADER_H

#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <unordered_map>
#include "model_loader.h"
#include "mesh.h"

namespace kpengine::asset
{
    class AssimpModelLoader : public IModelLoader
    {
    public:
        virtual bool Load(const std::string& path, ModelGeometryType type,  AssetRegisterInfo &info) override;
    private:
        AssetID LoadMesh(const std::string& path);
        void ProcessNode(aiNode *node, const aiScene *scene, MeshPtr esource, std::unordered_map<Vertex, uint32_t, VertexHash> &unique_vertices);
        void ProcessMesh(aiMesh *mesh, const aiScene *scene, MeshPtr resource, std::unordered_map<Vertex, uint32_t, VertexHash> &unique_vertices);
        Assimp::Importer import;
    
    };
}

#endif