#include "assimp_model_loader.h"
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/matrix3x3.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <magic_enum/magic_enum.hpp>
#include <string>
#include "log/logger.h"
#include "asset_manager.h"
#include "model.h"
#include "utility.h"
namespace kpengine::asset
{
    struct Assimp_ModelLoader::ImportTransform
    {
        aiMatrix4x4 value;
    };

    namespace
    {
        constexpr float kMinimumImportedVectorLength = 1.0e-6f;

        Vector3f ToVector3(const aiVector3D &value)
        {
            return {static_cast<float>(value.x), static_cast<float>(value.y),
                    static_cast<float>(value.z)};
        }

        Vector3f NormalizeImportedVector(const aiVector3D &value)
        {
            const float length = static_cast<float>(value.Length());
            if (!std::isfinite(length) || length <= kMinimumImportedVectorLength)
            {
                return {};
            }
            return ToVector3(value) * (1.0f / length);
        }

        std::string ReadTexturePath(const aiMaterial &material, aiTextureType type)
        {
            aiString path;
            if (material.GetTexture(type, 0, &path) != AI_SUCCESS)
            {
                return {};
            }
            return path.C_Str();
        }

        void ReadMaterialMetadata(const aiScene &scene, MeshData &mesh_data)
        {
            mesh_data.materials.resize(scene.mNumMaterials);
            for (uint32_t index = 0; index < scene.mNumMaterials; ++index)
            {
                const aiMaterial *const source = scene.mMaterials[index];
                MeshMaterial &destination = mesh_data.materials[index];
                if (source == nullptr)
                {
                    continue;
                }

                aiString name;
                if (source->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
                {
                    destination.name = name.C_Str();
                }

                aiColor4D color{1.0f, 1.0f, 1.0f, 1.0f};
                if (source->Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS)
                {
                    (void)source->Get(AI_MATKEY_COLOR_DIFFUSE, color);
                }
                destination.base_color = {color.r, color.g, color.b, color.a};

                float factor = 0.0f;
                if (source->Get(AI_MATKEY_METALLIC_FACTOR, factor) == AI_SUCCESS)
                {
                    destination.metallic = factor;
                }
                if (source->Get(AI_MATKEY_ROUGHNESS_FACTOR, factor) == AI_SUCCESS)
                {
                    destination.roughness = factor;
                }
                if (source->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS)
                {
                    destination.emissive = {color.r, color.g, color.b, color.a};
                }

                destination.base_color_texture =
                    ReadTexturePath(*source, aiTextureType_BASE_COLOR);
                if (destination.base_color_texture.empty())
                {
                    destination.base_color_texture =
                        ReadTexturePath(*source, aiTextureType_DIFFUSE);
                }
                destination.normal_texture = ReadTexturePath(*source, aiTextureType_NORMALS);
                destination.metallic_roughness_texture =
                    ReadTexturePath(*source, aiTextureType_UNKNOWN);
                if (destination.metallic_roughness_texture.empty())
                {
                    destination.metallic_roughness_texture =
                        ReadTexturePath(*source, aiTextureType_METALNESS);
                }
                destination.occlusion_texture =
                    ReadTexturePath(*source, aiTextureType_AMBIENT_OCCLUSION);
                destination.emissive_texture =
                    ReadTexturePath(*source, aiTextureType_EMISSIVE);

                aiString alpha_mode;
                if (source->Get(AI_MATKEY_GLTF_ALPHAMODE, alpha_mode) == AI_SUCCESS)
                {
                    destination.alpha_blended = std::string(alpha_mode.C_Str()) == "BLEND";
                }
            }
        }
    }

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

        ReadMaterialMetadata(*scene, *mesh_asset->data);
        ImportTransform root_transform{};
        ProcessNode(scene->mRootNode, scene, mesh_asset, root_transform, unique_vertices);
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
        for (const MeshSection &section : mesh_asset->data->sections)
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

    void Assimp_ModelLoader::ProcessNode(const aiNode *node, const aiScene *scene,
                                         MeshPtr mesh_asset,
                                         const ImportTransform &parent_transform,
                                         std::unordered_map<Vertex, uint32_t, VertexHash> &unique_vertices)
    {
        if (node == nullptr || scene == nullptr || mesh_asset == nullptr)
        {
            return;
        }

        const ImportTransform node_transform{
            parent_transform.value * node->mTransformation};
        for (uint32_t i = 0; i < node->mNumMeshes; i++)
        {
            const uint32_t mesh_index = node->mMeshes[i];
            if (mesh_index < scene->mNumMeshes && scene->mMeshes[mesh_index] != nullptr)
            {
                ProcessMesh(scene->mMeshes[mesh_index], node_transform, mesh_asset,
                            unique_vertices);
            }
        }

        for (uint32_t i = 0; i < node->mNumChildren; i++)
        {
            ProcessNode(node->mChildren[i], scene, mesh_asset, node_transform, unique_vertices);
        }
    }
    void Assimp_ModelLoader::ProcessMesh(const aiMesh *mesh,
                                         const ImportTransform &node_transform,
                                         MeshPtr mesh_asset,
                                         std::unordered_map<Vertex, uint32_t, VertexHash> &unique_vertices)
    {
        if (mesh == nullptr || mesh_asset == nullptr || mesh->mVertices == nullptr)
        {
            return;
        }

        std::shared_ptr<MeshData> resource = mesh_asset->data;
        uint32_t index_start = static_cast<uint32_t>(resource->indices.size());
        const bool has_normal = mesh->HasNormals();
        const bool has_texcoord = mesh->mTextureCoords[0];
        const bool has_tangent_and_bitangent = mesh->HasTangentsAndBitangents();
        const aiMatrix3x3 linear_transform(node_transform.value);
        aiMatrix3x3 normal_transform = linear_transform;
        normal_transform.Inverse().Transpose();

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
                if (vertex_index >= mesh->mNumVertices)
                {
                    continue;
                }
                vertex.position = ToVector3(node_transform.value * mesh->mVertices[vertex_index]);

                if (has_normal)
                {
                    vertex.normal = NormalizeImportedVector(
                        normal_transform * mesh->mNormals[vertex_index]);
                }
                if (has_texcoord)
                {
                    vertex.tex_coord = {mesh->mTextureCoords[0][vertex_index].x,
                                        mesh->mTextureCoords[0][vertex_index].y};
                }
                if (has_tangent_and_bitangent)
                {
                    vertex.tangent = NormalizeImportedVector(
                        linear_transform * mesh->mTangents[vertex_index]);
                    vertex.bitangent = NormalizeImportedVector(
                        linear_transform * mesh->mBitangents[vertex_index]);
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
        section.material_index = mesh->mMaterialIndex;

        resource->sections.push_back(section);
    }

}
