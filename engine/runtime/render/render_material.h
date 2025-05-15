#ifndef KPENGINE_RUNTIME_RENDER_MATERIAL_H
#define KPENGINE_RUNTIME_RENDER_MATERIAL_H

#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include "runtime/core/math/math_header.h"
namespace kpengine{

    class RenderTexture; 
    class RenderShader;

    namespace material_map_type{
        constexpr const char* DIFFUSE_MAP = "diffuse_map";
        constexpr const char* SPECULAR_MAP = "specular_map";
        constexpr const char* ALBEDO_MAP = "albedo_map";
        constexpr const char* NORMAL_MAP = "normal_map";
        constexpr const char* ROUGHNESS_MAP = "roughness_map";
        constexpr const char* METALLIC_MAP = "metallic_map";
        constexpr const char* AO_MAP = "ao_map";
    };

    namespace material_param_type{
        constexpr const char* SHINESS_PARAM = "shininess";
        constexpr const char* ALBEDO_PARAM = "albedo";
        constexpr const char* ROUGHNESS_PARAM = "roughness";
        constexpr const char* METALLIC_PARAM = "metallic";
        constexpr const char* AO_PARAM = "ao";
    };

    struct MaterialMapInfo{
        std::string map_type;
        std::string map_path;
    };

    struct MaterialFloatParamInfo{
        std::string param_type;
        float value;
    };

    struct MaterialVec3ParamInfo{
        std::string param_type;
        Vector3f value;
    };


    class RenderMaterial{
    
    public:
        static std::shared_ptr<RenderMaterial> CreatePBRMaterial(const std::vector<MaterialMapInfo>& map_info_container, const std::vector<MaterialFloatParamInfo>& float_param_info_container, const std::vector<MaterialVec3ParamInfo>& vec3_param_info_container);
        static std::shared_ptr<RenderMaterial> CreatePhongMaterial(const std::vector<MaterialMapInfo>& map_info_container, const std::vector<MaterialFloatParamInfo>& float_param_info_container, const std::vector<MaterialVec3ParamInfo>& vec3_param_info_container);
        static std::shared_ptr<RenderMaterial> CreateMaterial(const std::string& shader_name);
        
        void Initialize(const std::shared_ptr<RenderShader>& shader);
        void Render(const std::shared_ptr<RenderShader>& shader) ;
        void AddTexture(const MaterialMapInfo& map_info);

        std::shared_ptr<RenderTexture> GetTexture(const std::string key);
        float* GetFloatParamRef(const std::string& key);
        Vector3f* GetVectorParamRef(const std::string& key);

    public:
        std::shared_ptr<RenderShader> shader_;
        std::unordered_map<std::string, std::shared_ptr<RenderTexture>> textures;
        std::unordered_map<std::string, float> float_params;
        std::unordered_map<std::string, Vector3f> vec3_params;
        Vector3f diffuse_albedo_{1.f};

        unsigned int last_use_shader_id_ = 0;
    };



}

#endif