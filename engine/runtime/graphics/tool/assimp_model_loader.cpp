#include "assimp_model_loader.h"
#include "log/logger.h"
namespace kpengine::graphics
{
    bool AssimpModelLoader::Load(const std::string &path, MeshData &resource)
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

        std::unordered_map<Vertex, uint32_t, VertexHash> unique_vertices{};
        ProcessNode(scene->mRootNode, scene, resource, unique_vertices);
        return true;
    }

    void AssimpModelLoader::ProcessNode(aiNode *node, const aiScene *scene, MeshData &resource, std::unordered_map<Vertex, uint32_t, VertexHash> &unique_vertices)
    {
        if (node == nullptr)
        {
            return;
        }

        for (uint32_t i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            ProcessMesh(mesh, scene, resource, unique_vertices);
        }

        for (uint32_t i = 0; i < node->mNumChildren; i++)
        {
            ProcessNode(node->mChildren[i], scene, resource, unique_vertices);
        }
    }
    void AssimpModelLoader::ProcessMesh(aiMesh *mesh, const aiScene *scene, MeshData &resource, std::unordered_map<Vertex, uint32_t, VertexHash> &unique_vertices)
    {
        uint32_t index_start = static_cast<uint32_t>(resource.indices.size());
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

            if (unique_vertices.find(vertex) == unique_vertices.end())
            {
                uint32_t index = static_cast<uint32_t>(resource.vertices.size());
                unique_vertices[vertex] = index;
                resource.vertices.push_back(vertex);
            }
            resource.indices.push_back(unique_vertices[vertex]);
        }

        uint32_t index_count = static_cast<uint32_t>(resource.indices.size()) - index_start;

        MeshSection section{};
        section.index_start = index_start;
        section.index_count = index_count;

        resource.sections.push_back(section);
    }

}