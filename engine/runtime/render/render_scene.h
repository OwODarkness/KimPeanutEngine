#ifndef KPENGINE_RUNTIME_RENDER_SCENE_H
#define KPENGINE_RUNTIME_RENDER_SCENE_H

#include <memory>
#include <vector>

#include "render_light.h"
#include "scene_proxy_handle.h"

namespace kpengine{

    class FrameBuffer;
    class RenderObject;
    class RenderCamera;
    class RenderShader;
    class ShadowMaker;
    class PointShadowMaker;
    class Skybox;
    class PrimitiveSceneProxy;

    class RenderScene{
    public:
        RenderScene() = default;
        void Initialize(std::shared_ptr<RenderCamera> camera);
        void Render(float delta_time);
        SceneProxyHandle AddProxy(std::shared_ptr<PrimitiveSceneProxy> scene_proxy);
        void RemoveProxy(SceneProxyHandle handle);
    private:
        void ConfigureUniformLight(std::shared_ptr<RenderShader> shader);
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

        float angle = 0.f;
        std::shared_ptr<Skybox> skybox;
    private:
        std::vector<std::shared_ptr<PrimitiveSceneProxy>> scene_proxies;//renderable
        std::vector<unsigned int> free_slots;
        unsigned int current_generation = 0;

        unsigned int ubo_matrices_;
        bool isskydraw = false;
        bool is_light_dirty = true;
    };
}

#endif