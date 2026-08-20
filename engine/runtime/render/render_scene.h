#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SCENE_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SCENE_H

#include <cstdint>
#include <vector>

#include "graphics/backend/common/api.h"
#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_backend.h"
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
        graphics::RenderBackend *backend = nullptr;
        RenderSceneResources resources;
    };

    // The demo reappears here as the render module's first real scene: it owns the
    // mesh, texture, uniform buffers and descriptor sets, and records the frame's
    // draws through the API-neutral command recorder.
    class RenderScene
    {
    public:
        RenderScene() = default;
        ~RenderScene() = default;

        void Initialize(const RenderSceneInitInfo &info);
        void Tick(float delta_time);
        void Record(graphics::CommandRecorder &recorder);
        void Cleanup();

        // Scene/view state belongs here; the system only schedules this scene.
        RenderCamera &GetCamera() { return camera_; }
        const RenderCamera &GetCamera() const { return camera_; }

    private:
        struct UniformBuffer
        {
            std::vector<graphics::BufferHandle> handles;
            std::vector<void *> mapped;
            uint32_t element_size = 0;
        };

        void CreateUniformBuffers();
        void CreateDescriptorSets();
        void UpdateUniformBuffers(uint32_t frame_index);

    private:
        graphics::RenderBackend *backend_ = nullptr;
        RenderCamera camera_;
        graphics::PipelineHandle pipeline_handle_;
        graphics::TextureHandle texture_handle_;
        graphics::SamplerHandle sampler_handle_;
        graphics::MeshHandle mesh_handle_;
        UniformBuffer per_pass_ubo_;
        UniformBuffer per_object_ubo_;
        std::vector<graphics::DescriptorSetHandle> descriptor_sets_;
    };
}

#endif
