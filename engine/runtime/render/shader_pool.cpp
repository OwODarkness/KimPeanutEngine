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
            std::make_shared<RenderShader>("phong.vs", "phong.fs")
        });
        shader_cache.insert({
            SHADER_CATEGORY_SKYBOX,
            std::make_shared<RenderShader>("skybox.vs", "skybox.fs")
        });

        shader_cache.insert({
            SHADER_CATEGORY_NORMAL,
            std::make_shared<RenderShader>("normal.vs", "normal.fs")
        });

        shader_cache.insert({
            SHADER_CATEGORY_POINTCLOUD,
            std::make_shared<RenderShader>("pointcloud.vs", "pointcloud.fs")
        });

        shader_cache.insert({
            SHADER_CATEGORY_PBR,
            std::make_shared<RenderShader>("pbr.vs", "pbr.fs")
        });

        shader_cache.insert({
            SHADER_CATEGORY_EQUIRECTANGULAR,
            std::make_shared<RenderShader>("equiretangular_to_cubemap.vs", "equiretangular_to_cubemap.fs")
        });

        shader_cache.insert({
            SHADER_CATEGORY_IRRADIANCE,
            std::make_shared<RenderShader>("irradiance.vs", "irradiance.fs")
        });

        shader_cache.insert({
            SHADER_CATEGORY_PREFILTER,
            std::make_shared<RenderShader>("prefilter_map.vs", "prefilter_map.fs")
        });

        shader_cache.insert({
            SHADER_CATEGORY_BRDF,
            std::make_shared<RenderShader>("brdf.vs", "brdf.fs")
        });

        shader_cache.insert({
            SHADER_CATEGORY_DEBUGCUBIC,
            std::make_shared<RenderShader>("debug_cubic.vs", "debug_cubic.fs")
        });

        shader_cache.insert({
            SHADER_CATEGORY_AXIS,
            std::make_shared<RenderShader>("axis.vs", "axis.fs")
        });

        shader_cache.insert({
            SHADER_CATEGORY_OUTLINING,
            std::make_shared<RenderShader>("screen_quad.vs", "outline.fs")
        });
        
        shader_cache.insert({
            SHADER_CATEGORY_DEFER_PBR,
            std::make_shared<RenderShader>("defer_pbr.vs", "defer_pbr.fs")
        });

                shader_cache.insert({
            SHADER_CATEGORY_DIRECTIONALSHADOW,
            std::make_shared<RenderShader>("directionalshadow_mapping_depth.vs", "directionalshadow_mapping_depth.fs")
        });

                shader_cache.insert({
            SHADER_CATEGORY_POINTSHADOW,
            std::make_shared<RenderShader>("pointshadow_mapping_depth.vs", "pointshadow_mapping_depth.fs", "pointshadow_mapping_depth.gs")
        });
    }

    void ShaderPool::Initialize()
    {
        KP_LOG("ShaderManagerLog", LOG_LEVEL_DISPLAY, "shader compiling...");
        std::unordered_map<std::string, std::shared_ptr<RenderShader>>::iterator iter;
        for(iter = shader_cache.begin();iter !=shader_cache.end();++iter)
        {
            if(iter->second)
            {
                iter->second->Initialize();
            }
        }
        KP_LOG("ShaderManagerLog", LOG_LEVEL_DISPLAY, "shader compile end");

    }

    std::shared_ptr<RenderShader> ShaderPool::GetShader(const std::string& shader_type)
    {
        if(!shader_cache.contains(shader_type))
        {
            return nullptr;
        }
        return shader_cache.at(shader_type);
    }

    ShaderPool::~ShaderPool() = default;
}