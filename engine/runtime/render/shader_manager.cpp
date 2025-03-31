#include "shader_manager.h"

#include "runtime/render/render_shader.h"
#include "platform/path/path.h"
#include "runtime/core/log/logger.h"


namespace kpengine{
    ShaderManager::ShaderManager()
    {
        std::string shader_dir = GetShaderDirectory();
        shader_cache.insert(
        {
            SHADER_CATEGORY_PHONG, 
            std::make_shared<RenderShader>(shader_dir + "phong.vs", shader_dir + "phong.fs")
        });
        shader_cache.insert({
            SHADER_CATEGORY_SKYBOX,
            std::make_shared<RenderShader>(shader_dir + "skybox.vs", shader_dir + "skybox.fs")
        });
    }

    void ShaderManager::Initialize()
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

    std::shared_ptr<RenderShader> ShaderManager::GetShader(const std::string& shader_type)
    {
        if(!shader_cache.contains(shader_type))
        {
            return nullptr;
        }
        return shader_cache.at(shader_type);
    }

    ShaderManager::~ShaderManager() = default;
}