#ifndef KPENGINE_RUNTIME_SHADER_MANAGER_H
#define KPENGINE_RUNTIME_SHADER_MANAGER_H

#include <memory>
#include <string>
#include <unordered_map>


#define SHADER_CATEGORY_PHONG "phong_shader"
#define SHADER_CATEGORY_SKYBOX "skybox_shader"
#define SHADER_CATEGORY_NORMAL "normal_shader"
#define SHADER_CATEGORY_POINTCLOUD "pointcloud_shader"
#define SHADER_CATEGORY_PBR "pbr_shader"
#define SHADER_CATEGORY_EQUIRECTANGULAR "equirectangular_to_cubemap_shader"
#define SHADER_CATEGORY_IRRADIANCE "irradiance_shader"
#define SHADER_CATEGORY_PREFILTER "prefilter_map"
#define SHADER_CATEGORY_BRDF "brdf"
#define SHADER_CATEGORY_AXIS "axis"
#define SHADER_CATEGORY_DEBUGCUBIC "debug_cubic"
#define SHADER_CATEGORY_OUTLINING "outlining"

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