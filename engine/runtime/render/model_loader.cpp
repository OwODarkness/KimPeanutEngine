#include "model_loader.h"

#include "runtime/render/render_mesh.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_texture.h"
#include "runtime/core/log/logger.h"
#include "platform/path/path.h"

namespace kpengine
{
    std::vector<std::shared_ptr<RenderMesh>> ModelLoader::Load(const std::string &path)
    {
        Assimp::Importer import;
        const aiScene *scene = import.ReadFile(path, aiProcess_Triangulate | 
                                             aiProcess_GenNormals |
                                             aiProcess_FlipUVs);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            KP_LOG("ModelLoadLog", LOG_LEVEL_ERROR, "%s failed to load model", import.GetErrorString());
            return {};
        }
        directory = path.substr(0, path.find_last_of('/'));
        ProcessNode(scene->mRootNode, scene);
        return meshes;
    }

    void ModelLoader::ProcessNode(struct aiNode *node, const struct aiScene *scene)
    {
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(GenerateMesh(mesh, scene));
        }

        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            ProcessNode(node->mChildren[i], scene);
        }
    }

    std::shared_ptr<RenderMesh> ModelLoader::GenerateMesh(aiMesh *mesh, const aiScene *scene)
    {

        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        std::shared_ptr<RenderMaterialStanard> material = std::make_shared<RenderMaterialStanard>();

        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
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

        for (unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            for (unsigned int j = 0; j < mesh->mFaces[i].mNumIndices; j++)
            {
                indices.push_back(mesh->mFaces[i].mIndices[j]);
            }
        }

        if (mesh->mMaterialIndex > 0)
        {
            
            aiMaterial *ai_material = scene->mMaterials[mesh->mMaterialIndex];

            ProcessTexture(ai_material, aiTextureType_DIFFUSE, material->diffuse_textures_);
            ProcessTexture(ai_material, aiTextureType_SPECULAR, material->specular_textures_);
        }
        else
        {
            
            std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture2D>(GetTextureDirectory() + "default.jpg");
            texture->Initialize();
            material->diffuse_textures_.push_back(texture);
        }

        return std::make_shared<RenderMeshStandard>(vertices, indices, material);
    }

    void ModelLoader::ProcessTexture(aiMaterial *material, aiTextureType assimp_texture_type, std::vector<std::shared_ptr<RenderTexture>> &textures)
    {
        for (unsigned int i = 0; i < material->GetTextureCount(assimp_texture_type); i++)
        {
            aiString file_path;
            material->GetTexture(assimp_texture_type, i, &file_path);

            file_path = directory + '/' + file_path.C_Str();

            bool is_texture_cached = false;
            for (const auto &item : textures_cached)
            {
                if (item->image_path_ == file_path.C_Str())
                {
                    textures.push_back(item);
                    is_texture_cached = true;
                    break;
                }
            }

            if (false == is_texture_cached)
            {
                std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture2D>(file_path.C_Str());
                texture->Initialize();
                textures.push_back(texture);
                textures_cached.push_back(texture);
            }
        }
    }
}