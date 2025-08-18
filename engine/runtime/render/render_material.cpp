#include "render_material.h"

#include <glad/glad.h>
#include <iostream>
#include <iostream>
#include "runtime/render/render_shader.h"
#include "runtime/render/render_texture.h"
#include "runtime/runtime_header.h"
#include "runtime/render/texture_pool.h"
namespace kpengine
{
    std::shared_ptr<RenderMaterial> RenderMaterial::CreatePBRMaterial(const std::vector<MaterialMapInfo> &map_info_container, const std::vector<MaterialFloatParamInfo> &float_param_info_container, const std::vector<MaterialVec3ParamInfo> &vec3_param_info_container)
    {
        std::shared_ptr<RenderMaterial> material = std::make_shared<RenderMaterial>();
        material->shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_PBR);
        TexturePool *texture_pool = runtime::global_runtime_context.render_system_->GetTexturePool();

        material->textures.insert({material_map_type::ALBEDO_MAP,
                                   nullptr});
        material->textures.insert({material_map_type::ROUGHNESS_MAP,
                                   nullptr});
        material->textures.insert({material_map_type::METALLIC_MAP,
                                   nullptr});
        material->textures.insert({material_map_type::NORMAL_MAP,
                                   nullptr});
        material->textures.insert({material_map_type::AO_MAP,
                                   nullptr});

        for (const MaterialMapInfo &map_info : map_info_container)
        {
            if (!map_info.map_path.empty() && material->textures.contains(map_info.map_type))
            {
                bool is_texture_color = map_info.map_type == material_map_type::ALBEDO_MAP ? true : false;
                std::shared_ptr<RenderTexture> texture = texture_pool->FetchTexture2D(map_info.map_path, is_texture_color);
                if (texture)
                {
                    material->textures[map_info.map_type] = texture;
                }
            }
        }

        for (const MaterialFloatParamInfo &float_param_info : float_param_info_container)
        {
            if (float_param_info.param_type != "")
            {
                material->float_params.insert({float_param_info.param_type, float_param_info.value});
            }
        }

        for (const MaterialVec3ParamInfo &vec3_param_info : vec3_param_info_container)
        {
            if (vec3_param_info.param_type != "")
            {
                material->vec3_params.insert({vec3_param_info.param_type, vec3_param_info.value});
            }
        }

        return material;
    }
    std::shared_ptr<RenderMaterial> RenderMaterial::CreatePhongMaterial(const std::vector<MaterialMapInfo> &map_info_container, const std::vector<MaterialFloatParamInfo> &float_param_info_container, const std::vector<MaterialVec3ParamInfo> &vec3_param_info_container)
    {
        std::shared_ptr<RenderMaterial> material = std::make_shared<RenderMaterial>();
        material->shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_PHONG);
        TexturePool *texture_pool = runtime::global_runtime_context.render_system_->GetTexturePool();

        material->textures.insert({material_map_type::DIFFUSE_MAP,
                                   nullptr});
        material->textures.insert({material_map_type::SPECULAR_MAP,
                                   nullptr});
        material->textures.insert({material_map_type::NORMAL_MAP,
                                   nullptr});
        material->textures.insert({material_map_type::BUMP_MAP,
                                   nullptr});

        for (const MaterialMapInfo &map_info : map_info_container)
        {
            if (!map_info.map_path.empty() && material->textures.contains(map_info.map_type))
            {
                std::shared_ptr<RenderTexture> texture = texture_pool->FetchTexture2D(map_info.map_path);
                if (texture)
                {
                    material->textures[map_info.map_type] = texture;
                }
            }
        }

        material->float_params.insert({material_param_type::SHINESS_PARAM, 70.f});
        material->vec3_params.insert({material_param_type::ALBEDO_PARAM, Vector3f(1.f)});

        for (const MaterialFloatParamInfo &float_param_info : float_param_info_container)
        {
            if (float_param_info.param_type != "")
            {
                material->float_params.insert({float_param_info.param_type, float_param_info.value});
            }
        }

        for (const MaterialVec3ParamInfo &vec3_param_info : vec3_param_info_container)
        {
            if (vec3_param_info.param_type != "")
            {
                material->vec3_params.insert({vec3_param_info.param_type, vec3_param_info.value});
            }
        }
        return material;
    }

    std::shared_ptr<RenderMaterial> RenderMaterial::CreateMaterial(const std::string &shader_name)
    {
        std::shared_ptr<RenderMaterial> material = std::make_shared<RenderMaterial>();
        material->shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(shader_name);
        return material;
    }

    void RenderMaterial::AddTexture(const MaterialMapInfo &map_info)
    {

        if (map_info.map_path != "" && textures.contains(map_info.map_type))
        {
            TexturePool *texture_pool = runtime::global_runtime_context.render_system_->GetTexturePool();
            textures[map_info.map_type] = texture_pool->FetchTexture2D(map_info.map_path);
        }
    }

    void RenderMaterial::Initialize(const std::shared_ptr<RenderShader> &shader)
    {
        shader->UseProgram();
        int map_count = 0;
        for (const auto &pair : textures)
        {
            std::string texture_name = "material." + pair.first;

            shader->SetInt(texture_name, map_count);
            map_count++;
        }
    }

    std::shared_ptr<RenderTexture> RenderMaterial::GetTexture(const std::string key) const
    {
        if (textures.contains(key))
        {
            return textures.at(key);
        }
        else
        {
            return nullptr;
        }
    }

    float *RenderMaterial::GetFloatParamRef(const std::string &key)
    {
        if (float_params.contains(key))
        {
            return &float_params[key];
        }
        else
        {
            return nullptr;
        }
    }

    Vector3f *RenderMaterial::GetVectorParamRef(const std::string &key)
    {
        if (vec3_params.contains(key))
        {
            return &vec3_params[key];
        }
        else
        {
            return nullptr;
        }
    }

    int RenderMaterial::Render(const std::shared_ptr<RenderShader> &shader, size_t texture_start_index) const
    {
        // Initialize(shader);

        for (const auto &pair : float_params)
        {
            std::string param_name = "material." + pair.first;
            shader->SetFloat(param_name, pair.second);
        }

        for (const auto &pair : vec3_params)
        {
            std::string param_name = "material." + pair.first;
            shader->SetVec3(param_name, pair.second.Data());
        }

        size_t map_count = 0;
        for (const auto &pair : textures)
        {
            if (pair.second)
            {
                std::string bool_name = "material.has_" + pair.first;
                shader->SetBool(bool_name, true);
                std::string texture_name = "material." + pair.first;
                shader->SetInt(texture_name, texture_start_index + map_count);
                glActiveTexture(GL_TEXTURE0 + texture_start_index + map_count);
                glBindTexture(GL_TEXTURE_2D, pair.second->GetTexture());

                map_count++;
            }
            else
            {
                std::string bool_name = "material.has_" + pair.first;
                shader->SetBool(bool_name, false);
            }
        }

        glActiveTexture(GL_TEXTURE0);
        return map_count;
    }

}