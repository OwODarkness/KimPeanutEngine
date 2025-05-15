#ifndef KPENGINE_RUNTIME_ENVIRONMENT_MAP_H
#define KPENGINE_RUNTIME_ENVIRONMENT_MAP_H

#include <memory>
#include <string>


namespace kpengine{
    class RenderShader;
    class RenderTexture;

class EnvironmentMapWrapper{
public:
    EnvironmentMapWrapper(const std::string& hdr_path);
    bool Initialize();
    std::shared_ptr<RenderTexture> GetEnvironmentMap() const{return environment_map_;}
    std::shared_ptr<RenderTexture> GetIrradianceMap() const{return irradiance_map_;}
private:
    void RenderCube();
    void GenerateEnvironmentMap();
    void GenerateIrradianceMap();
private:
    std::shared_ptr<RenderShader> equirect_to_cubemap_shader_;
    std::shared_ptr<RenderShader> irradiance_shader_;
    std::shared_ptr<RenderTexture> hdr_texture_;
    std::shared_ptr<RenderTexture> environment_map_;
    std::shared_ptr<RenderTexture> irradiance_map_;

    std::string hdr_path_;
    unsigned int capture_fbo_;
    unsigned int capture_rbo_; 

    unsigned int environment_map_handle_;
    unsigned int irradiance_map_handle_;
    static unsigned int vao;
    static unsigned int vbo;
    static const float vertices[108];
};

}

#endif