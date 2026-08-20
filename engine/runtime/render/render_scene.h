#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SCENE_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SCENE_H

#include <cstdint>
#include "graphics/backend/common/api.h"
#include "graphics/backend/common/command_recorder.h"
#include "frame_context.h"
#include "render_camera.h"
#include "render_resource.h"

namespace kpengine::render
{
    struct RenderSceneResources
    {
        graphics::PipelineHandle pipeline;
        graphics::MeshHandle mesh;
        TextureBinding material;
    };

    struct RenderSceneInitInfo
    {
        RenderSceneResources resources;
    };

    // The demo scene owns only logical camera/renderable/material state. Frame
    // uniform storage and descriptor sets are allocated by the supplied context.
    class RenderScene
    {
    public:
        RenderScene() = default;
        ~RenderScene() = default;

        void Initialize(const RenderSceneInitInfo &info);
        void Record(FrameContext &frame, graphics::CommandRecorder &recorder);
        void Cleanup();

        // Scene/view state belongs here; the system only schedules this scene.
        RenderCamera &GetCamera() { return camera_; }
        const RenderCamera &GetCamera() const { return camera_; }

    private:
        RenderCamera camera_;
        graphics::PipelineHandle pipeline_handle_;
        graphics::TextureHandle texture_handle_;
        graphics::SamplerHandle sampler_handle_;
        graphics::MeshHandle mesh_handle_;
    };
}

#endif
