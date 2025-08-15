#ifndef KPENGINE_RUNTIME_RENDER_SCENE_H
#define KPENGINE_RUNTIME_RENDER_SCENE_H

#define KPENGINE_MAX_LIGHTS 100

#include <memory>
#include <vector>

#include "render_context.h"
#include "scene_proxy_handle.h"

namespace kpengine
{

    class FrameBuffer;
    class RenderCamera;
    class RenderShader;
    class PointShadowMaker;
    class ShadowManager;
    class Skybox;
    class PrimitiveSceneProxy;
    class EnvironmentMapWrapper;
    class Gizmos;
    class PostProcessPipeline;

    class RenderScene
    {
    public:
        RenderScene();
        void Initialize(std::shared_ptr<RenderCamera> camera);
        void Render(float delta_time);
        SceneProxyHandle AddProxy(std::shared_ptr<PrimitiveSceneProxy> scene_proxy);
        void RemoveProxy(SceneProxyHandle handle);
        void SetCurrentShader(const std::shared_ptr<RenderShader> &shader);
        void SetRenderAxis(std::shared_ptr<Gizmos> gizmos);
        ~RenderScene();

    private:
        void InitFullScreenTriangle();
        void ExecLightingRenderPass(const RenderContext &context);

    public:
        std::shared_ptr<FrameBuffer> scene_fb_; // frame buffer
        std::shared_ptr<FrameBuffer> g_buffer_;

        std::shared_ptr<RenderCamera> render_camera_;

        std::shared_ptr<Skybox> skybox;
        std::shared_ptr<EnvironmentMapWrapper> environment_map_wrapper;

        unsigned int debug_id;

    private:
        // scene proxy
        bool is_skybox_visible = true;
        bool is_light_dirty = true;

        int width_;
        int height_;
        unsigned int current_generation{};
        unsigned int ubo_camera_matrices_{}; // unifrom buffer object
        unsigned int light_ssbo_{};
        unsigned int fullscreen_vao{};
        unsigned int fullscreen_vbo{};

        std::shared_ptr<RenderShader> current_shader;
        std::shared_ptr<RenderShader> geometry_shader_;
        std::shared_ptr<RenderShader> light_pass_shader_{};
        std::shared_ptr<PostProcessPipeline> postprocess_pipeline_;
        std::unique_ptr<ShadowManager> shadow_manager_;
        std::vector<std::shared_ptr<struct LightData>> lights_;
        std::vector<std::shared_ptr<PrimitiveSceneProxy>> scene_proxies;
        std::shared_ptr<Gizmos> gizmos_;
        std::vector<unsigned int> free_slots;
    };
}

#endif