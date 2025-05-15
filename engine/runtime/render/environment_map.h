#ifndef KPENGINE_RUNTIME_ENVIRONMENT_MAP_H
#define KPENGINE_RUNTIME_ENVIRONMENT_MAP_H

#include <memory>
#include <string>


namespace kpengine{
    class RenderShader;
    class RenderTexture;

class EnvironmentMap{
public:
    EnvironmentMap(const std::string& hdr_path);
    bool Initialize();
    void RenderCube();
private:
    std::shared_ptr<RenderShader> shader_;
    std::shared_ptr<RenderTexture> hdr_texture_;
    static unsigned int vao;
    static unsigned int vbo;
    static const float vertices[108];
};

}

#endif