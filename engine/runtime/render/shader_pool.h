#ifndef KPENGINE_RUNTIME_SHADER_MANAGER_H
#define KPENGINE_RUNTIME_SHADER_MANAGER_H


#define SHADER_CATEGORY_PHONG "phong_shader"
#define SHADER_CATEGORY_SKYBOX "skybox_shader"


#include <memory>
#include <string>
#include <unordered_map>


namespace kpengine{
    class RenderShader;
    
    class ShaderPool{
    public:
        ShaderPool();
        void Initialize();
        ~ShaderPool();
        std::shared_ptr<RenderShader> GetShader(const std::string& shader_type);
    private:
        std::unordered_map<std::string, std::shared_ptr<RenderShader>> shader_cache;
    };
}

#endif //KPENGINE_RUNTIME_SHADER_MANAGER_H