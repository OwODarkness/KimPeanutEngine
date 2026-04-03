#include "assimp_model_loader.h"
#include <assimp/postprocess.h>
#include <magic_enum/magic_enum.hpp>
#include "log/logger.h"
#include "asset_manager.h"
#include "model.h"
#include "utility.h"
namespace kpengine::asset
{

    bool AssimpModelLoader::Load(const std::string &path, ModelGeometryType type, AssetRegisterInfo &info )
    {

        AssetID id{};
        if (type == ModelGeometryType::KPMG_Mesh)
        {
            id = LoadMesh(path);
        }
        if (id.IsValid())
        {

            auto model_ptr = std::get_if<ModelPtr>(&info.resource);
            std::shared_ptr<ModelResource> resource;

            if (model_ptr)
            {
                resource = *model_ptr;
            }
            if (!resource)
            {
                resource = std::make_shared<ModelResource>();
                info.resource = resource;
            }
            info.type = AssetType::KPAT_Model;
            info.path = path;
            std::string type_string = std::string(magic_enum::enum_name(info.type));
            info.name = type_string + "_" + ExtractNameFromPath(path);
            info.dependencies.push_back(id);
            resource->BindData(type, id);
            return true;
        }
        return false;
    }

    AssetID AssimpModelLoader::LoadMesh(const std::string &path)
    {

        const aiScene *scene = import.ReadFile(
            path,
            aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            KP_LOG("ModelLoadLog", LOG_LEVEL_ERROR, "%s failed to load model", import.GetErrorString());
            return AssetID();
        }

        std::unordered_map<Vertex, uint32_t, VertexHash> unique_vertices{};
        std::shared_ptr<MeshResource> mesh_asset = std::make_shared<MeshResource>();

        ProcessNode(scene->mRootNode, scene, mesh_asset, unique_vertices);
        uint32_t face_count = 0;
        for(auto section : mesh_asset->data->sections)
        {
            face_count += section.index_count;
        }
        mesh_asset->face_count  = face_count;
        mesh_asset->vertex_count = static_cast<uint32_t>(mesh_asset->data->vertices.size());
        std::string name = ExtractNameFromPath(path);
        AssetRegisterInfo info{};
        info.resource = mesh_asset;
        info.path = path;
        info.type = AssetType::KPAT_Mesh;
        std::string type_string = std::string(magic_enum::enum_name(info.type));
        info.name = type_string + "_" + name;

        return AssetManager::GetInstance().RegisterAsset(info);
    }

    void AssimpModelLoader::ProcessNode(aiNode *node, const aiScene *scene, MeshPtr mesh_asset, std::unordered_map<Vertex, uint32_t, VertexHash> &unique_vertices)
    {
        if (node == nullptr)
        {
            return;
        }

        for (uint32_t i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            ProcessMesh(mesh, scene, mesh_asset, unique_vertices);
        }

        for (uint32_t i = 0; i < node->mNumChildren; i++)
        {
            ProcessNode(node->mChildren[i], scene, mesh_asset, unique_vertices);
        }
    }
    void AssimpModelLoader::ProcessMesh(aiMesh *mesh, const aiScene *scene, MeshPtr mesh_asset, std::unordered_map<Vertex, uint32_t, VertexHash> &unique_vertices)
    {
        std::shared_ptr<MeshData> resource = mesh_asset->data;
        uint32_t index_start = static_cast<uint32_t>(resource->indices.size());
        const uint32_t vertex_count = static_cast<uint32_t>(resource->vertices.size());
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
                uint32_t index = static_cast<uint32_t>(resource->vertices.size());
                unique_vertices[vertex] = index;
                resource->vertices.push_back(vertex);
            }
            resource->indices.push_back(unique_vertices[vertex]);
        }

        uint32_t index_count = static_cast<uint32_t>(resource->indices.size()) - index_start;

        MeshSection section{};
        section.index_start = index_start;
        section.index_count = index_count;

        resource->sections.push_back(section);
    }

}