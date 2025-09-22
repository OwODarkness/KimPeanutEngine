#include "assimp_model_loader.h"
#include "log/logger.h"
namespace kpengine::graphics
{
    bool AssimpModelLoader::Load(const std::string &path, MeshResource &resource)
    {
        Assimp::Importer import;
        const aiScene *scene = import.ReadFile(
            path,
            aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            KP_LOG("ModelLoadLog", LOG_LEVEL_ERROR, "%s failed to load model", import.GetErrorString());
            return false;
        }
        ProcessNode(scene->mRootNode, scene, resource);
        return true;
    }

    void AssimpModelLoader::ProcessNode(aiNode *node, const aiScene *scene, MeshResource &resource)
    {
        if (node == nullptr)
        {
            return;
        }

        for (uint32_t i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            ProcessMesh(mesh, scene, resource);
        }

        for (uint32_t i = 0; i < node->mNumChildren; i++)
        {
            ProcessNode(node->mChildren[i], scene, resource);
        }
    }
    void AssimpModelLoader::ProcessMesh(aiMesh *mesh, const aiScene *scene, MeshResource &resource)
    {
        const uint32_t vertex_count = static_cast<uint32_t>(resource.vertices.size());
        const bool has_normal = mesh->HasNormals();
        const bool has_texcoord = mesh->mTextureCoords[0];
        const bool has_tangent_and_bitangent = mesh->HasTangentsAndBitangents();
        for (uint32_t i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex{};

            vertex.position = {mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z};
            
            if (has_normal)
            {
                vertex.normal = {mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
            }
            if (has_texcoord)
            {
                vertex.tex_coord = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
            }
            if (has_tangent_and_bitangent)
            {
                vertex.tangent = {mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z};
                vertex.bitangent = {mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z};
            }

            resource.vertices.push_back(vertex);
        }

        const uint32_t index_start = static_cast<uint32_t>(resource.indices.size());
        uint32_t index_count = 0;
        for(uint32_t i = 0;i<mesh->mNumFaces;i++)
        {
            for(uint32_t j = 0;j<mesh->mFaces[i].mNumIndices;j++)
            {
                uint32_t vertex_index = mesh->mFaces[i].mIndices[j] + vertex_count;
                resource.indices.push_back(vertex_index);
            }
            index_count += (mesh->mNumFaces) * (mesh->mFaces[i].mNumIndices);
        }

        MeshSection section{};
        section.index_start = index_start;
        section.index_count = index_count;
 
        resource.sections.push_back(section);
    }

}