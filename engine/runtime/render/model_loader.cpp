#include "model_loader.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "runtime/render/render_mesh.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_texture.h"
#include "runtime/core/log/logger.h"
namespace kpengine
{
    std::vector<std::shared_ptr<RenderMesh>> ModelLoader::Load(const std::string& path)
    {
        Assimp::Importer import;
        std::filesystem::path full_path = model_directory_path / path;
        const aiScene *scene = import.ReadFile(full_path.generic_string(), aiProcess_Triangulate | aiProcess_FlipUVs);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            KP_LOG("ModelLoadLog", LOG_LEVEL_ERROR, "%s failed to load model from %s");
            return {};
        }
        directory = path.substr(0, path.find_last_of('\\'));
        ProcessNode(scene->mRootNode, scene);
        return meshes;
    }

    void ModelLoader::ProcessNode(struct aiNode *node, const struct aiScene *scene)
    {
        for (int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(GenerateMesh(mesh, scene));
        }

        for (int i = 0; i < node->mNumChildren; i++)
        {
            ProcessNode(node->mChildren[i], scene);
        }
    }

    std::shared_ptr<RenderMesh> ModelLoader::GenerateMesh(aiMesh *mesh, const aiScene *scene)
    {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        std::shared_ptr<RenderMaterial> material = std::make_shared<RenderMaterial>();


        for (int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex;
            vertex.location = glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
            vertex.normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
            if (mesh->mTextureCoords[0])
            {
                vertex.tex_coord = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
            }

            vertices.push_back(vertex);
        }

        for (int i = 0; i < mesh->mNumFaces; i++)
        {
            for (int j = 0; j < mesh->mFaces[i].mNumIndices; j++)
            {
                indices.push_back(mesh->mFaces[i].mIndices[j]);
            }
        }

        if(mesh->mMaterialIndex >=0 )
        {
            aiMaterial* ai_material = scene->mMaterials[mesh->mMaterialIndex];
            ProcessTexture(ai_material, aiTextureType_DIFFUSE, material->diffuse_textures_);
            ProcessTexture(ai_material, aiTextureType_DIFFUSE, material->specular_textures_);
        }

        return std::make_shared<RenderMesh>(vertices, indices, material);
    }

    void ModelLoader::ProcessTexture(aiMaterial* material, aiTextureType assimp_texture_type, std::vector<std::shared_ptr<RenderTexture>>& textures)
    {
        for(int i = 0;i<material->GetTextureCount(assimp_texture_type);i++)
        {
            aiString file_path;
            material->GetTexture(assimp_texture_type, i, &file_path);

            file_path = directory + '\\' + file_path.C_Str();

            bool is_texture_cached = false;
            for(const auto& item: textures_cached)
            {
                if(item->image_path_ == file_path.C_Str())
                {
                    textures.push_back(item);
                    is_texture_cached = true;
                    break;
                }
            }
    
            if(false == is_texture_cached)
            {
                std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture>(file_path.C_Str());
                texture->Initialize();
                textures.push_back(texture);
                textures_cached.push_back(texture);
            }
    }


}