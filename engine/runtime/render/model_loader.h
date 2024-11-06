#ifndef KPENGINE_RUNTIME_MODEL_LOADER_H
#define KPENGINE_RUNTIME_MODEL_LOADER_H

#include <string>
#include <vector>
#include <memory>
#include <filesystem>

struct aiNode;
struct aiScene;
struct aiMaterial;
namespace kpengine{
    const std::filesystem::path model_directory_path = "../../engine/asset/model";

    class RenderMesh;
    class RenderTexture;

    class ModelLoader{
    public:
        ModelLoader() = default;

        std::vector<std::shared_ptr<RenderMesh>> Load(const std::string& path);
        void ProcessNode(aiNode* node ,const aiScene* scene);
        std::shared_ptr<RenderMesh> GenerateMesh(aiMesh* mesh, const aiScene* scene);
        void ProcessTexture(aiMaterial* material, aiTextureType assimp_texture_type, std::vector<std::shared_ptr<RenderTexture>>& textures);
    private:
        std::string directory;
        std::vector<std::shared_ptr<RenderMesh>> meshes;
        std::vector<std::shared_ptr<RenderTexture>> textures_cached;
    };
}

#endif