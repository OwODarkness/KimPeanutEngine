#ifndef KPENGINE_RUNTIME_MODEL_LOADER_H
#define KPENGINE_RUNTIME_MODEL_LOADER_H

#include <string>
#include <vector>
#include <memory>
#include <filesystem>

#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

#include "runtime/render/mesh_vertex.h"

namespace kpengine{

    class RenderMesh;
    class RenderTexture;
    class RenderMeshResource;

    //this no state class to used to load model to get mesh data 
    class ModelLoader{
    public:
        //output: mesh_resource
        //path is relative directory
        static bool Load(const std::string& path, RenderMeshResource& mesh_resource);
    private:
        void ProcessNode(aiNode* node ,const aiScene* scene, RenderMeshResource& mesh_resource);
        void CountMeshData(aiNode* node, const aiScene* scene, unsigned int& vertices_num, unsigned int& indices_num);
        void ProcessMesh(aiMesh* mesh, const aiScene* scene, RenderMeshResource& mesh_resource);  
        void ProcessTexture(aiMaterial* material, aiTextureType assip_texture_type,  std::vector<std::shared_ptr<RenderTexture>>& textures);
        std::string directory;
    };
}

#endif