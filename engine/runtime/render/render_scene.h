#ifndef KPENGINE_RUNTIME_RENDER_SCENE_H
#define KPENGINE_RUNTIME_RENDER_SCENE_H

#include <memory>
#include <vector>

#include "render_light.h"
#include "scene_proxy_handle.h"

namespace kpengine{

    class FrameBuffer;
    class RenderCamera;
    class RenderShader;
    class ShadowMaker;
    class PointShadowMaker;
    class Skybox;
    class PrimitiveSceneProxy;
    class EnvironmentMap;

    class RenderScene{
    public:
        RenderScene();
        void Initialize(std::shared_ptr<RenderCamera> camera);
        void Render(float delta_time);
        SceneProxyHandle AddProxy(std::shared_ptr<PrimitiveSceneProxy> scene_proxy);
        void RemoveProxy(SceneProxyHandle handle);
        void SetCurrentShader(const std::shared_ptr<RenderShader>& shader);
    public:
        std::shared_ptr<FrameBuffer> scene_;//frame buffer

        std::shared_ptr<RenderCamera> render_camera_;

        Light light_;

        std::shared_ptr<ShadowMaker> directional_shadow_maker_;
        std::shared_ptr<ShadowMaker> point_shadow_maker_;

        float angle = 0.f;
        std::shared_ptr<Skybox> skybox;
        std::shared_ptr<EnvironmentMap> environment_map;
    private:
        //scene proxy 
        std::vector<std::shared_ptr<PrimitiveSceneProxy>> scene_proxies;//renderable
        std::vector<unsigned int> free_slots;
        unsigned int current_generation = 0;

        unsigned int ubo_camera_matrices_;//unifrom buffer object
        unsigned int ubo_light_;//unifrom buffer object

        bool isskydraw = false;
        bool is_light_dirty = true;

        std::shared_ptr<RenderShader> current_shader;
    };
}

#endif