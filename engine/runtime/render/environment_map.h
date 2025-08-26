#ifndef KPENGINE_RUNTIME_ENVIRONMENT_MAP_H
#define KPENGINE_RUNTIME_ENVIRONMENT_MAP_H

#include <memory>
#include <string>
#include <vector>

#include "math/math_header.h"


namespace kpengine{
    class RenderShader;
    class RenderTexture;

class EnvironmentMapWrapper{
public:
    EnvironmentMapWrapper(const std::string& hdr_path);
    bool Initialize();

    std::shared_ptr<RenderTexture> GetEnvironmentMap() const{return environment_map_;}
    std::shared_ptr<RenderTexture> GetIrradianceMap() const{return irradiance_map_;}
    std::shared_ptr<RenderTexture> GetPrefilterMap() const{return prefilter_map_;}
    std::shared_ptr<RenderTexture> GetBRDFMap() const{return brdf_map_;}
private:
    void RenderCube();
    void RenderQuad();
    void GenerateEnvironmentMap(const Matrix4f& proj,  const std::vector<Matrix4f>& views);
    void GenerateIrradianceMap(const Matrix4f& proj,  const std::vector<Matrix4f>& views);
    void GeneratePrefilterMap(const Matrix4f& proj,  const std::vector<Matrix4f>& views);
    void GenerateBRDFMap();
private:
    std::shared_ptr<RenderShader> equirect_to_cubemap_shader_;
    std::shared_ptr<RenderShader> irradiance_shader_;
    std::shared_ptr<RenderShader> prefilter_shader_;
    std::shared_ptr<RenderShader> brdf_shader_;

    std::shared_ptr<RenderTexture> hdr_texture_;
    std::shared_ptr<RenderTexture> environment_map_;
    std::shared_ptr<RenderTexture> irradiance_map_;
    std::shared_ptr<RenderTexture> prefilter_map_;
    std::shared_ptr<RenderTexture> brdf_map_;

    std::string hdr_path_;
    unsigned int capture_fbo_;
    unsigned int capture_rbo_; 

    unsigned int environment_map_handle_;
    unsigned int irradiance_map_handle_;
    unsigned int prefilter_map_handle_;
    unsigned int brdf_map_handle_;

    static unsigned int cube_vao;
    static unsigned int cube_vbo;

    static unsigned int quad_vao;
    static unsigned int quad_vbo;

    static const float cube_vertices[108];

    static const float quad_vertices[20];
};

}

#endif