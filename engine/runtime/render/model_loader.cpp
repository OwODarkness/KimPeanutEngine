#include "model_loader.h"

#include <iostream>

#include "runtime/render/render_mesh.h"
#include "runtime/render/render_material.h"
#include "runtime/render/render_texture.h"
#include "runtime/core/log/logger.h"
#include "platform/path/path.h"

#include "runtime/render/render_mesh_resource.h"
#include "runtime/runtime_header.h"
#include "runtime/render/shader_pool.h"
namespace kpengine
{

    //ModelLoader_V2
    bool ModelLoader::Load(const std::string& relative_model_path, RenderMeshResource& mesh_resource){
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
        //KP_LOG("ModelLoadLog", LOG_LEVEL_DISPLAY, "start load mesh based model from %s", absolute_model_path.c_str());
        
        
        ModelLoader model_loader;
        model_loader.directory = relative_model_path.substr(0, relative_model_path.find_last_of('/'));
        //preallocate vertex_buffer and index_buffer
        unsigned int vertices_num = 0;
        unsigned int indices_num = 0;
        
        model_loader.CountMeshData(scene->mRootNode, scene, vertices_num, indices_num);
        mesh_resource.vertex_buffer_.reserve(vertices_num);
        mesh_resource.index_buffer_.reserve(indices_num);
        model_loader.ProcessNode(scene->mRootNode, scene, mesh_resource);
        return true;
    }



    void ModelLoader::ProcessNode(aiNode* node ,const aiScene* scene, RenderMeshResource& mesh_resource)
    {
        if(node == nullptr)
        {
            return ;
        }
        
        for(unsigned int i = 0;i<node->mNumMeshes;i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            ProcessMesh(mesh, scene, mesh_resource);
        }

        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            ProcessNode(node->mChildren[i], scene, mesh_resource);
        }
    }

    void ModelLoader::CountMeshData(aiNode* node, const aiScene* scene, unsigned int& vertices_num, unsigned int& indices_num)
    {
        if(node == nullptr)
        {
            return ;
        }
        for(unsigned int i = 0;i<node->mNumMeshes;i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            vertices_num += mesh->mNumVertices;
            
            for(unsigned int j = 0;j< mesh->mNumFaces;j++)
            {
                indices_num += mesh->mFaces[j].mNumIndices;
            }
        }

        for(unsigned int i = 0;i<node->mNumChildren;i++)
        {
            CountMeshData(node->mChildren[i], scene, vertices_num, indices_num);
        }
    }

    void ModelLoader::ProcessMesh(aiMesh* mesh, const aiScene* scene, RenderMeshResource& mesh_resource)
    {
        unsigned int mesh_count =  (unsigned int)mesh_resource.vertex_buffer_.size();
        //TODO load texture and wrap to material
        MeshSection mesh_section;
        mesh_section.index_start = static_cast<unsigned int>(mesh_resource.index_buffer_.size());
        

        bool has_normal = mesh->HasNormals();
        bool has_texcoord = mesh->mTextureCoords[0];
        //extract vertices
        for(unsigned int i = 0;i<mesh->mNumVertices;i++)
        {
            MeshVertex vertex;
            //position
            vertex.position = {mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z};
            //normal
            if(has_normal)
            {
                vertex.normal = {mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
            }
            //texcoord
            if(has_texcoord)
            {
                vertex.tex_coord = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
            }

            mesh_resource.vertex_buffer_.push_back(vertex);
        }
        //generate index buffer
        unsigned int current_index_count = 0;
        for(unsigned int i = 0;i<mesh->mNumFaces;i++)
        {
            for(unsigned int j = 0; j< mesh->mFaces[i].mNumIndices;j++)
            {
                unsigned int vertex_index = mesh->mFaces[i].mIndices[j] + mesh_count;
                mesh_resource.index_buffer_.push_back(vertex_index);
                current_index_count++;   
            }
        }
        mesh_section.index_count = current_index_count;
        mesh_section.face_count = mesh->mNumFaces;
        //generate material
        std::shared_ptr<RenderMaterial> material = std::make_shared<RenderMaterial>();
        material->shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_PHONG);
            
        if (mesh->mMaterialIndex > 0)
        {
            aiMaterial *ai_material = scene->mMaterials[mesh->mMaterialIndex];
            ProcessTexture(ai_material, aiTextureType_DIFFUSE, material->diffuse_textures_);
            ProcessTexture(ai_material, aiTextureType_SPECULAR, material->specular_textures_);
        }
        else
        {

            //without texture attach, send a default texture
            TexturePool* texture_pool = runtime::global_runtime_context.render_system_->GetTexturePool();
            if(texture_pool->IsTextureCached("texture/default.jpg"))
            {
                material->diffuse_textures_.push_back(texture_pool->FindTextureByKey("texture/default.jpg"));
            }
            else
            {
                std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture2D>("texture/default.jpg");
                bool is_succeed = texture->Initialize();
                if(is_succeed)
                {
                    texture_pool->AddTexture(texture);
                    material->diffuse_textures_.push_back(texture);
                }
            }
        }
        
        mesh_section.material = material;
        mesh_resource.mesh_sections_.push_back(mesh_section);


    }

    void ModelLoader::ProcessTexture(aiMaterial *material, aiTextureType assimp_texture_type, std::vector<std::shared_ptr<RenderTexture>> &textures)
    {
        for (unsigned int i = 0; i < material->GetTextureCount(assimp_texture_type); i++)
        {
            aiString file_path;
            material->GetTexture(assimp_texture_type, i, &file_path);

            file_path = directory + '/' + file_path.C_Str();
            std::string texture_key = file_path.C_Str();
            bool is_texture_cached = false;

            TexturePool* texture_pool = runtime::global_runtime_context.render_system_->GetTexturePool();
            is_texture_cached = texture_pool->IsTextureCached(texture_key);

            if(is_texture_cached == true)
            {
                textures.push_back(texture_pool->FindTextureByKey(texture_key));
            }
            else
            {
                std::shared_ptr<RenderTexture> texture = std::make_shared<RenderTexture2D>(texture_key);
                bool is_succeed = texture->Initialize();
                if(is_succeed)
                {
                    texture_pool->AddTexture(texture);
                    textures.push_back(texture);
                }
            }


        }
    }
}