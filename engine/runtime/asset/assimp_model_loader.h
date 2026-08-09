#ifndef KPENGINE_RUNTIME_ASSET_ASSIMP_MODEL_LOADER_H
#define KPENGINE_RUNTIME_ASSET_ASSIMP_MODEL_LOADER_H

#include <memory>
#include <unordered_map>

#include "model_loader.h"
#include "mesh.h"

// Forward declarations keep all Assimp types out of this public header, so
// consumers only need "model_loader.h"/"mesh.h" and never Assimp include paths.
namespace Assimp
{
    class Importer;
}

struct aiNode;
struct aiScene;
struct aiMesh;

namespace kpengine::asset
{
    class Assimp_ModelLoader : public IModelLoader
    {
    public:
        Assimp_ModelLoader();
        ~Assimp_ModelLoader() override;
        Assimp_ModelLoader(const Assimp_ModelLoader&) = delete;
        Assimp_ModelLoader& operator=(const Assimp_ModelLoader&) = delete;

        virtual bool Load(const std::string& path, ModelGeometryType type,  AssetRegisterInfo &info) override;
    private:
        AssetID LoadMesh(const std::string& path);
        void ProcessNode(aiNode *node, const aiScene *scene, MeshPtr resource, std::unordered_map<Vertex, uint32_t, VertexHash> &unique_vertices);
        void ProcessMesh(aiMesh *mesh, const aiScene *scene, MeshPtr resource, std::unordered_map<Vertex, uint32_t, VertexHash> &unique_vertices);

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif
