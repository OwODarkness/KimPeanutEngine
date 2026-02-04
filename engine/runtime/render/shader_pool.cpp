#include "shader_pool.h"

#include "render_shader.h"
#include "config/path.h"
#include "log/logger.h"


namespace kpengine{
    ShaderPool::ShaderPool()
    {
        shader_cache.insert(
        {
            SHADER_CATEGORY_PHONG, 
            std::make_shared<RenderShader>("phong", "phong")
        });
        shader_cache.insert({
            SHADER_CATEGORY_SKYBOX,
            std::make_shared<RenderShader>("skybox", "skybox")
        });

        shader_cache.insert({
            SHADER_CATEGORY_NORMAL,
            std::make_shared<RenderShader>("normal", "normal")
        });

        shader_cache.insert({
            SHADER_CATEGORY_POINTCLOUD,
            std::make_shared<RenderShader>("pointcloud", "pointcloud")
        });

        shader_cache.insert({
            SHADER_CATEGORY_PBR,
            std::make_shared<RenderShader>("pbr", "pbr")
        });

        shader_cache.insert({
            SHADER_CATEGORY_EQUIRECTANGULAR,
            std::make_shared<RenderShader>("equiretangular_to_cubemap", "equiretangular_to_cubemap")
        });

        shader_cache.insert({
            SHADER_CATEGORY_IRRADIANCE,
            std::make_shared<RenderShader>("irradiance", "irradiance")
        });

        shader_cache.insert({
            SHADER_CATEGORY_PREFILTER,
            std::make_shared<RenderShader>("prefilter_map", "prefilter_map")
        });

        shader_cache.insert({
            SHADER_CATEGORY_BRDF,
            std::make_shared<RenderShader>("brdf", "brdf")
        });

        shader_cache.insert({
            SHADER_CATEGORY_DEBUGCUBIC,
            std::make_shared<RenderShader>("debug_cubic", "debug_cubic")
        });

        shader_cache.insert({
            SHADER_CATEGORY_AXIS,
            std::make_shared<RenderShader>("axis", "axis")
        });

        shader_cache.insert({
            SHADER_CATEGORY_OUTLINING,
            std::make_shared<RenderShader>("screen_quad", "outline")
        });
        
        shader_cache.insert({
            SHADER_CATEGORY_DEFER_PBR,
            std::make_shared<RenderShader>("defer_pbr", "defer_pbr")
        });

                shader_cache.insert({
            SHADER_CATEGORY_DIRECTIONALSHADOW,
            std::make_shared<RenderShader>("directionalshadow_mapping_depth", "directionalshadow_mapping_depth")
        });

                shader_cache.insert({
            SHADER_CATEGORY_POINTSHADOW,
            std::make_shared<RenderShader>("pointshadow_mapping_depth", "pointshadow_mapping_depth", "pointshadow_mapping_depth")
        });
    }

    void ShaderPool::Initialize()
    {
        KP_LOG("ShaderManagerLog", LOG_LEVEL_INFO, "shader compiling...");
        std::unordered_map<std::string, std::shared_ptr<RenderShader>>::iterator iter;
        for(iter = shader_cache.begin();iter !=shader_cache.end();++iter)
        {
            if(iter->second)
            {
                iter->second->Initialize();
            }
        }
        KP_LOG("ShaderManagerLog", LOG_LEVEL_INFO, "shader compile end");

    }

    std::shared_ptr<RenderShader> ShaderPool::GetShader(const std::string& shader_type)
    {
        if(shader_cache.find(shader_type) == shader_cache.end())
        {
            return nullptr;
        }
        return shader_cache.at(shader_type);
    }

    ShaderPool::~ShaderPool() = default;
}