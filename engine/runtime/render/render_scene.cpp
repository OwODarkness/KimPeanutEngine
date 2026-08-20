#include "render_scene.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "log/logger.h"
#include "graphics/backend/common/mesh.h"
#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/texture.h"
#include "graphics/backend/common/texture_manager.h"
#include "graphics/backend/common/sampler_manager.h"
#include "graphics/backend/common/mesh_manager.h"
#include "math/math_header.h"

namespace kpengine::render
{
#define KP_RENDER_SCENE_LOG_NAME "RenderSceneLog"

    namespace
    {
        // Matches VulkanFrameContext::MAX_FRAMES_IN_FLIGHT — one UBO + descriptor
        // set per frame in flight.
        constexpr uint32_t kMaxFramesInFlight = 2;
    }

    void RenderScene::Initialize(const RenderSceneInitInfo &info)
    {
        if (!info.backend || !info.resources.pipeline.IsValid() || !info.resources.mesh.IsValid() ||
            !info.resources.material.texture.IsValid() || !info.resources.material.sampler.IsValid())
        {
            throw std::runtime_error("RenderScene requires valid backend and static resource handles");
        }
        backend_ = info.backend;
        pipeline_handle_ = info.resources.pipeline;
        mesh_handle_ = info.resources.mesh;
        texture_handle_ = info.resources.material.texture;
        sampler_handle_ = info.resources.material.sampler;
        CreateUniformBuffers();
        CreateDescriptorSets();
    }

    void RenderScene::CreateUniformBuffers()
    {
        per_pass_ubo_.element_size = static_cast<uint32_t>(sizeof(graphics::PerPassData));
        per_pass_ubo_.handles.resize(kMaxFramesInFlight);
        per_pass_ubo_.mapped.resize(kMaxFramesInFlight);
        per_object_ubo_.element_size = static_cast<uint32_t>(sizeof(graphics::PerObjectData));
        per_object_ubo_.handles.resize(kMaxFramesInFlight);
        per_object_ubo_.mapped.resize(kMaxFramesInFlight);

        for (uint32_t i = 0; i < kMaxFramesInFlight; i++)
        {
            per_pass_ubo_.handles[i] = backend_->CreateUniformBuffer(per_pass_ubo_.element_size);
            per_pass_ubo_.mapped[i] = backend_->MapUniformBuffer(per_pass_ubo_.handles[i], per_pass_ubo_.element_size);
            per_object_ubo_.handles[i] = backend_->CreateUniformBuffer(per_object_ubo_.element_size);
            per_object_ubo_.mapped[i] = backend_->MapUniformBuffer(per_object_ubo_.handles[i], per_object_ubo_.element_size);
        }
    }

    void RenderScene::CreateDescriptorSets()
    {
        descriptor_sets_.resize(kMaxFramesInFlight);
        for (uint32_t i = 0; i < kMaxFramesInFlight; i++)
        {
            graphics::ResourceBindingSetDesc desc{};
            desc.set = 0;
            desc.bindings = {
                graphics::UniformBufferBinding{0, 0, per_pass_ubo_.handles[i], 0,
                                               per_pass_ubo_.element_size},
                graphics::UniformBufferBinding{0, 1, per_object_ubo_.handles[i], 0,
                                               per_object_ubo_.element_size},
                graphics::SampledTextureBinding{0, 2, texture_handle_, sampler_handle_},
            };
            descriptor_sets_[i] = backend_->CreateResourceBindingSet(pipeline_handle_, desc);
            if (!descriptor_sets_[i].IsValid())
            {
                throw std::runtime_error("Failed to create resource binding set");
            }
        }
    }

    void RenderScene::UpdateUniformBuffers(uint32_t frame_index)
    {
        static auto start_time = std::chrono::high_resolution_clock::now();
        auto current_time = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();

        graphics::PerPassData per_pass_data{};
        const graphics::Extent2D extent = backend_->GetRenderExtent();
        float aspect = extent.width / (float)extent.height;
        camera_.SetAspect(aspect);
        const CameraData camera_data = camera_.GetCameraData();
        per_pass_data.camera_data.view = camera_data.view;
        per_pass_data.camera_data.proj = camera_data.proj;

        Transform3f model{};
        model.scale_ = {0.5f, 0.5f, 0.5f};
        model.rotator_.pitch_ = time * 90.f;

        graphics::PerObjectData per_object_data{};
        per_object_data.model = Matrix4f::MakeTransformMatrix(model).Transpose();

        if (per_pass_ubo_.mapped[frame_index])
        {
            std::memcpy(per_pass_ubo_.mapped[frame_index], &per_pass_data, per_pass_ubo_.element_size);
        }
        if (per_object_ubo_.mapped[frame_index])
        {
            std::memcpy(per_object_ubo_.mapped[frame_index], &per_object_data, per_object_ubo_.element_size);
        }
    }

    void RenderScene::Tick(float delta_time)
    {
        (void)delta_time;
        UpdateUniformBuffers(backend_->GetCurrentFrameIndex());
    }

    void RenderScene::Record(graphics::CommandRecorder &recorder)
    {
        const uint32_t frame_index = backend_->GetCurrentFrameIndex();
        recorder.BindPipeline(pipeline_handle_);
        recorder.BindMesh(mesh_handle_);
        recorder.BindResourceBindings(pipeline_handle_, descriptor_sets_[frame_index]);
        recorder.DrawIndexed();
    }

    void RenderScene::Cleanup()
    {
        if (!backend_)
        {
            return;
        }

        for (graphics::DescriptorSetHandle handle : descriptor_sets_)
        {
            backend_->DestroyResourceBindingSet(handle);
        }
        descriptor_sets_.clear();
        for (auto handle : per_pass_ubo_.handles)
        {
            backend_->DestroyBufferResource(handle);
        }
        for (auto handle : per_object_ubo_.handles)
        {
            backend_->DestroyBufferResource(handle);
        }
    }
}
