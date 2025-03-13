#ifndef KPENGINE_RUNTIME_RENDER_SCENE_H
#define KPENGINE_RUNTIME_RENDER_SCENE_H

#include <memory>
#include <vector>
#include "runtime/render/render_light.h"

namespace kpengine{
    class FrameBuffer;
    class RenderObject;
    class RenderCamera;
    class RenderShader;
    class ShadowMaker;
    class PointShadowMaker;
    class SkyBox;

    class RenderScene{
    public:
        RenderScene() = default;

        void Initialize(std::shared_ptr<RenderCamera> camera);


        void Render(float delta_time);

    private:
        void ConfigurePointLightInfo(std::shared_ptr<RenderShader> shader);
        void ConfigureSpotLightInfo(std::shared_ptr<RenderShader> shader);
        void ConfigureDirectionalLightInfo(std::shared_ptr<RenderShader> shader);
    public:
        std::shared_ptr<FrameBuffer> scene_;//frame buffer

        std::vector<std::shared_ptr<RenderObject>> render_objects_;

        std::shared_ptr<RenderCamera> render_camera_;

        AmbientLight ambient_light_;
        DirectionalLight directional_light_;
        PointLight point_light_;
        SpotLight spot_light_;

        std::shared_ptr<ShadowMaker> directional_shadow_maker_;
        std::shared_ptr<ShadowMaker> point_shadow_maker_;

        std::shared_ptr<RenderObject> skybox;

        float angle = 0.f;

    private:
        unsigned int ubo_matrices_;
        bool isskydraw = true;
    };
}

#endif