#include "assimp_model_loader.h"
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <algorithm>
#include <memory>
#include <magic_enum/magic_enum.hpp>
#include "log/logger.h"
#include "asset_manager.h"
#include "model.h"
#include "utility.h"
namespace kpengine::asset
{

    // Defined only here so the public header never needs Assimp.
    struct Assimp_ModelLoader::Impl
    {
        Assimp::Importer import;
    };

    Assimp_ModelLoader::Assimp_ModelLoader() : impl_(std::make_unique<Impl>()) {}
    Assimp_ModelLoader::~Assimp_ModelLoader() = default;

    bool Assimp_ModelLoader::Load(const std::string &path, ModelGeometryType type, AssetRegisterInfo &info )
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

    AssetID Assimp_ModelLoader::LoadMesh(const std::string &path)
    {

        const aiScene *scene = impl_->import.ReadFile(
            path,
            aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            KP_LOG("ModelLoadLog", LOG_LEVEL_ERROR, "%s failed to load model", impl_->import.GetErrorString());
            return AssetID();
        }

        std::unordered_map<Vertex, uint32_t, VertexHash> unique_vertices{};
        std::shared_ptr<MeshResource> mesh_asset = std::make_shared<MeshResource>();

        ProcessNode(scene->mRootNode, scene, mesh_asset, unique_vertices);
        if (!mesh_asset->data->vertices.empty())
        {
            spatial::AABB bounds{mesh_asset->data->vertices.front().position,
                                 mesh_asset->data->vertices.front().position};
            for (const Vertex &vertex : mesh_asset->data->vertices)
            {
                bounds.min_.x_ = std::min(bounds.min_.x_, vertex.position.x_);
                bounds.min_.y_ = std::min(bounds.min_.y_, vertex.position.y_);
                bounds.min_.z_ = std::min(bounds.min_.z_, vertex.position.z_);
                bounds.max_.x_ = std::max(bounds.max_.x_, vertex.position.x_);
                bounds.max_.y_ = std::max(bounds.max_.y_, vertex.position.y_);
                bounds.max_.z_ = std::max(bounds.max_.z_, vertex.position.z_);
            }
            mesh_asset->local_bounds = bounds;
        }
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

    void Assimp_ModelLoader::ProcessNode(aiNode *node, const aiScene *scene, MeshPtr mesh_asset, std::unordered_map<Vertex, uint32_t, VertexHash> &unique_vertices)
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
    void Assimp_ModelLoader::ProcessMesh(aiMesh *mesh, const aiScene *scene, MeshPtr mesh_asset, std::unordered_map<Vertex, uint32_t, VertexHash> &unique_vertices)
    {
        std::shared_ptr<MeshData> resource = mesh_asset->data;
        uint32_t index_start = static_cast<uint32_t>(resource->indices.size());
        const bool has_normal = mesh->HasNormals();
        const bool has_texcoord = mesh->mTextureCoords[0];
        const bool has_tangent_and_bitangent = mesh->HasTangentsAndBitangents();

        // Assimp's vertex array is not a draw-order index buffer. The faces
        // define which vertex records form each primitive; iterating
        // mNumVertices directly scrambles meshes whose face order differs
        // from vertex order (including the supplied rock asset).
        for (uint32_t face_index = 0; face_index < mesh->mNumFaces; ++face_index)
        {
            const aiFace &face = mesh->mFaces[face_index];
            for (uint32_t corner = 0; corner < face.mNumIndices; ++corner)
            {
                const uint32_t vertex_index = face.mIndices[corner];
                Vertex vertex{};
                vertex.position = {mesh->mVertices[vertex_index].x,
                                   mesh->mVertices[vertex_index].y,
                                   mesh->mVertices[vertex_index].z};

                if (has_normal)
                {
                    vertex.normal = {mesh->mNormals[vertex_index].x,
                                     mesh->mNormals[vertex_index].y,
                                     mesh->mNormals[vertex_index].z};
                }
                if (has_texcoord)
                {
                    vertex.tex_coord = {mesh->mTextureCoords[0][vertex_index].x,
                                        mesh->mTextureCoords[0][vertex_index].y};
                }
                if (has_tangent_and_bitangent)
                {
                    vertex.tangent = {mesh->mTangents[vertex_index].x,
                                      mesh->mTangents[vertex_index].y,
                                      mesh->mTangents[vertex_index].z};
                    vertex.bitangent = {mesh->mBitangents[vertex_index].x,
                                        mesh->mBitangents[vertex_index].y,
                                        mesh->mBitangents[vertex_index].z};
                }

                const auto [it, inserted] = unique_vertices.emplace(
                    vertex, static_cast<uint32_t>(resource->vertices.size()));
                if (inserted)
                {
                    resource->vertices.push_back(vertex);
                }
                resource->indices.push_back(it->second);
            }
        }

        uint32_t index_count = static_cast<uint32_t>(resource->indices.size()) - index_start;

        MeshSection section{};
        section.index_start = index_start;
        section.index_count = index_count;

        resource->sections.push_back(section);
    }

}
