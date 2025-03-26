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
        virtual void Initialize();
        virtual void BindFrameBuffer();
        virtual void UnBindFrameBuffer();
        virtual void CalculateShadowTransform(glm::vec3 light_position, std::vector<glm::mat4>& out_shadow_transforms) = 0;
   
        std::shared_ptr<RenderShader> GetShader() const {return shader;}
        inline unsigned int GetShadowMap() const {return depth_texture_;}
        inline unsigned int GetShadowBuffer() const{return frame_buffer_;}
        inline float GetNearPlane() const{return near_plane_;}
        inline float GetFarPlane() const{return far_plane_;}
    protected:
        unsigned int depth_texture_;
        unsigned int frame_buffer_;
        int width_{1024};
        int height_{1024};
        float near_plane_{1.f};
        float far_plane_{7.5f};
        std::shared_ptr<RenderShader> shader;
    };

    class DirectionalShadowMaker: public ShadowMaker{
    public:
        DirectionalShadowMaker(int width = 1024, int height = 1024);
        void Initialize() override;
        void BindFrameBuffer() override;
        void UnBindFrameBuffer() override;
        void CalculateShadowTransform(glm::vec3 light_position, std::vector<glm::mat4>& out_shadow_transform) override;
    };

    class PointShadowMaker : public ShadowMaker{
    public:
        PointShadowMaker(int width = 1024, int height = 1024);
        void Initialize() override;
        void BindFrameBuffer()override;
        void UnBindFrameBuffer()override;
        void CalculateShadowTransform(glm::vec3 light_position, std::vector<glm::mat4>& out_shadow_transform) override;

    };
}

#endif //KPENGINE_RUNTIME_SHADOW_MAKER_H
