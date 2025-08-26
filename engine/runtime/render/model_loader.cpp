#include "model_loader.h"
#include <iostream>
#include "render_mesh.h"
#include "render_material.h"
#include "render_texture.h"
#include "log/logger.h"
#include "config/path.h"
#include "render_pointcloud_resource.h"
#include "render_mesh_resource.h"
#include "shader_pool.h"


namespace kpengine
{
    bool ModelLoader::Load(const std::string &relative_model_path, RenderMeshResource &mesh_resource)
    {
        Assimp::Importer import;
        std::string absolute_model_path = GetAssetDirectory() + relative_model_path;

        const aiScene *scene = import.ReadFile(absolute_model_path, aiProcess_Triangulate |
                                                                        aiProcess_GenSmoothNormals |
                                                                        aiProcess_CalcTangentSpace);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            KP_LOG("ModelLoadLog", LOG_LEVEL_ERROR, "%s failed to load model", import.GetErrorString());
            return false;
        }
        // KP_LOG("ModelLoadLog", LOG_LEVEL_DISPLAY, "start load mesh based model from %s", absolute_model_path.c_str());

        ModelLoader model_loader;
        model_loader.directory = relative_model_path.substr(0, relative_model_path.find_last_of('/'));
        // preallocate vertex_buffer and index_buffer
        unsigned int vertices_num = 0;
        unsigned int indices_num = 0;

        model_loader.CountMeshData(scene->mRootNode, scene, vertices_num, indices_num);
        mesh_resource.vertex_buffer_.reserve(vertices_num);
        mesh_resource.index_buffer_.reserve(indices_num);
        model_loader.ProcessNode(scene->mRootNode, scene, mesh_resource);
        return true;
    }

    bool ModelLoader::Load(const std::string &relative_model_path, RenderPointCloudResource &pointcloud_resource)
    {
        Assimp::Importer import;
        std::string absolute_model_path = GetAssetDirectory() + relative_model_path;

        const aiScene *scene = import.ReadFile(absolute_model_path, aiProcess_Triangulate |
                                                                        aiProcess_GenNormals |
                                                                        aiProcess_FlipUVs);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            KP_LOG("ModelLoadLog", LOG_LEVEL_ERROR, "%s failed to load model", import.GetErrorString());
            return false;
        }

        ModelLoader model_loader;
        model_loader.directory = relative_model_path.substr(0, relative_model_path.find_last_of('/'));
        model_loader.ProcessNode(scene->mRootNode, scene, pointcloud_resource);
        // TODO provide a material to point cloud resource

        std::shared_ptr<RenderMaterial> material = RenderMaterial::CreateMaterial(SHADER_CATEGORY_POINTCLOUD);
        pointcloud_resource.material_ = material;
        return true;
    }

    void ModelLoader::ProcessNode(aiNode *node, const aiScene *scene, RenderMeshResource &mesh_resource)
    {
        if (node == nullptr)
        {
            return;
        }

        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            ProcessMesh(mesh, scene, mesh_resource);
        }

        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            ProcessNode(node->mChildren[i], scene, mesh_resource);
        }
    }

    void ModelLoader::CountMeshData(aiNode *node, const aiScene *scene, unsigned int &vertices_num, unsigned int &indices_num)
    {
        if (node == nullptr)
        {
            return;
        }
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            vertices_num += mesh->mNumVertices;

            for (unsigned int j = 0; j < mesh->mNumFaces; j++)
            {
                indices_num += mesh->mFaces[j].mNumIndices;
            }
        }

        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            CountMeshData(node->mChildren[i], scene, vertices_num, indices_num);
        }
    }

    void ModelLoader::ProcessMesh(aiMesh *mesh, const aiScene *scene, RenderMeshResource &mesh_resource)
    {
        unsigned int mesh_count = (unsigned int)mesh_resource.vertex_buffer_.size();
        MeshSection mesh_section;
        mesh_section.index_start = static_cast<unsigned int>(mesh_resource.index_buffer_.size());

        bool has_normal = mesh->HasNormals();
        bool has_texcoord = mesh->mTextureCoords[0];
        bool has_tangent_and_bitangent = mesh->HasTangentsAndBitangents();
        // extract vertices
        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            MeshVertex vertex;
            // position
            vertex.position = {mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z};
            // normal
            if (has_normal)
            {
                vertex.normal = {mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
            }
            // texcoord
            if (has_texcoord)
            {
                vertex.tex_coord = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
                if (has_tangent_and_bitangent)
                {
                    vertex.tangent = {mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z};
                    vertex.bitangent = {mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z};
                }
            }
            else
            {
                vertex.tex_coord = {0.f, 0.f};
                vertex.tangent = {0.f, 0.f, 0.f};
                vertex.bitangent = {0.f, 0.f, 0.f};
            }

            mesh_resource.vertex_buffer_.push_back(vertex);
        }

        // generate index buffer
        unsigned int current_index_count = 0;
        for (unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            for (unsigned int j = 0; j < mesh->mFaces[i].mNumIndices; j++)
            {
                unsigned int vertex_index = mesh->mFaces[i].mIndices[j] + mesh_count;
                mesh_resource.index_buffer_.push_back(vertex_index);
                current_index_count++;
            }
        }
        mesh_section.index_count = current_index_count;
        mesh_section.face_count = mesh->mNumFaces;

        // generate material
        if (mesh->mMaterialIndex > 0)
        {
            std::vector<MaterialMapInfo> map_info_container;

            aiMaterial *ai_material = scene->mMaterials[mesh->mMaterialIndex];
            std::string diffuse_name = ProcessTexture(ai_material, aiTextureType_DIFFUSE);

            bool is_phong_model = !diffuse_name.empty() ? true : false;

            if (is_phong_model)
            {
                if (!diffuse_name.empty())
                {
                    map_info_container.push_back({material_map_type::DIFFUSE_MAP, diffuse_name});
                }

                std::string specular_name = ProcessTexture(ai_material, aiTextureType_SPECULAR);
                if (!specular_name.empty())
                {
                    map_info_container.push_back({material_map_type::SPECULAR_MAP, specular_name});
                }

                std::string normal_name = ProcessTexture(ai_material, aiTextureType_NORMALS);
                if (!normal_name.empty())
                {
                    map_info_container.push_back({material_map_type::NORMAL_MAP, normal_name});
                }

                std::string bump_name = ProcessTexture(ai_material, aiTextureType_HEIGHT);
                if (!bump_name.empty())
                {
                    map_info_container.push_back({material_map_type::NORMAL_MAP, bump_name});
                }
                mesh_section.material = RenderMaterial::CreatePhongMaterial(map_info_container, {}, {});
            }
            else
            {
                std::string albedo = ProcessTexture(ai_material, aiTextureType_BASE_COLOR);
                if (!albedo.empty())
                {
                    std::cout << albedo << "\n";
                }
            }
        }
        else
        {
            // without texture attach, send a default texture
            std::vector<MaterialMapInfo> map_info_container;
            map_info_container.push_back({material_map_type::DIFFUSE_MAP, "texture/default.png"});
            std::vector<MaterialFloatParamInfo> float_param_container;

            mesh_section.material = RenderMaterial::CreatePhongMaterial(map_info_container, {}, {});
        }

        mesh_resource.mesh_sections_.push_back(mesh_section);
    }

    std::string ModelLoader::ProcessTexture(aiMaterial *material, aiTextureType assimp_texture_type)
    {
        if (material->GetTextureCount(assimp_texture_type) > 0)
        {
            aiString file_path;
            material->GetTexture(assimp_texture_type, 0, &file_path);
            file_path = directory + '/' + file_path.C_Str();
            return file_path.C_Str();
        }
        else
        {
            return "";
        }
    }

    void ModelLoader::ProcessNode(aiNode *node, const aiScene *scene, RenderPointCloudResource &pointcloud_resource)
    {
        if (node == nullptr)
        {
            return;
        }

        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            ProcessMesh(mesh, scene, pointcloud_resource);
        }

        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            ProcessNode(node->mChildren[i], scene, pointcloud_resource);
        }
    }

    void ModelLoader::ProcessMesh(aiMesh *mesh, const aiScene *scene, RenderPointCloudResource &pointcloud_resource)
    {
        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vector3f pos = {mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z};
            pointcloud_resource.vertex_buffer_.push_back(pos);
        }
    }

    void ModelLoader::ComputeTangent(aiMesh *mesh, const aiScene *scene, RenderMeshResource &mesh_resource)
    {
        // for(unsigned int i = 0;i<mesh-)
    }
}