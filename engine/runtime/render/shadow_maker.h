#ifndef KPENGINE_RUNTIME_SHADOW_MAKER_H
#define KPENGINE_RUNTIME_SHADOW_MAKER_H

#include <memory>
#include <vector>
#include <glm/gtc/type_ptr.hpp>

namespace kpengine{

    class RenderShader;
    
    class ShadowMaker{

    public:
        ShadowMaker(int width = 1024, int height = 1024);
        void Initialize();
        unsigned int GetShadowMap() const {return depth_texture_;}
        unsigned int GetShadowBuffer() const{return frame_buffer_;}
        // float GetNearPlane() const{return near_plane_;}
        // float GetFarPlane() const{return far_plane_;}
        std::shared_ptr<RenderShader> GetShader() const {return shader;}
        void BindFrameBuffer();
        void UnBindFrameBuffer();
        glm::mat4 CalculateShadowTransform(glm::vec3 light_position);
    private:
        unsigned int depth_texture_;
        unsigned int frame_buffer_;
        int width_{1024};
        int height_{1024};
        float near_plane_{1.f};
        float far_plane_{7.5f};
        std::shared_ptr<RenderShader> shader;
    };

    class PointShadowMaker{
    public:
        PointShadowMaker(int width, int height);
        void Initialize();
        unsigned int GetShadowMap() const{return depth_texture_;}
        std::shared_ptr<RenderShader> GetShader() const{return shader;}
        void BindFrameBuffer();
        void UnBindFrameBuffer();
        std::vector<glm::mat4> CalculateShadowTransform(const glm::vec3& light_position);

    private:
        unsigned int depth_texture_;
        unsigned int frame_buffer_;
        int width_{1024};
        int height_{1024};
        float near_plane_{1.f};
        float far_plane_{25.f};
        std::shared_ptr<RenderShader> shader;
    };
}

#endif
