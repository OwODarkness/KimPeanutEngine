#include "render_scene.h"

#include <stdexcept>

#include "math/math_header.h"

namespace kpengine::render
{
#define KP_RENDER_SCENE_LOG_NAME "RenderSceneLog"

    void RenderScene::Initialize(const RenderSceneInitInfo &info)
    {
        if (!info.resources.pipeline.IsValid() || !info.resources.mesh.IsValid() ||
            !info.resources.material.texture.IsValid() || !info.resources.material.sampler.IsValid())
        {
            throw std::runtime_error("RenderScene requires valid backend and static resource handles");
        }
        pipeline_handle_ = info.resources.pipeline;
        mesh_handle_ = info.resources.mesh;
        texture_handle_ = info.resources.material.texture;
        sampler_handle_ = info.resources.material.sampler;
    }

    void RenderScene::Record(FrameContext &frame, graphics::CommandRecorder &recorder)
    {
        if (!frame.IsActive())
        {
            return;
        }
        graphics::PerPassData per_pass_data{};
        const graphics::Extent2D extent = frame.GetRenderExtent();
        if (extent.height == 0)
        {
            return;
        }
        float aspect = extent.width / (float)extent.height;
        camera_.SetAspect(aspect);
        const CameraData camera_data = camera_.GetCameraData();
        per_pass_data.camera_data.view = camera_data.view;
        per_pass_data.camera_data.proj = camera_data.proj;

        Transform3f model{};
        model.scale_ = {0.5f, 0.5f, 0.5f};
        model.rotator_.pitch_ = frame.GetGlobals().elapsed_seconds * 90.f;

        graphics::PerObjectData per_object_data{};
        per_object_data.model = Matrix4f::MakeTransformMatrix(model).Transpose();

        const UniformAllocation per_pass = frame.AllocateUniform(per_pass_data);
        const UniformAllocation per_object = frame.AllocateUniform(per_object_data);
        if (!per_pass.IsValid() || !per_object.IsValid())
        {
            return;
        }

        graphics::ResourceBindingSetDesc bindings{};
        bindings.set = 0;
        bindings.bindings = {
            graphics::UniformBufferBinding{0, 0, per_pass.buffer, per_pass.offset, per_pass.range},
            graphics::UniformBufferBinding{0, 1, per_object.buffer, per_object.offset, per_object.range},
            graphics::SampledTextureBinding{0, 2, texture_handle_, sampler_handle_},
        };
        const graphics::DescriptorSetHandle binding_set =
            frame.AllocateResourceBindingSet(pipeline_handle_, bindings);
        if (!binding_set.IsValid())
        {
            return;
        }

        recorder.BindPipeline(pipeline_handle_);
        recorder.BindMesh(mesh_handle_);
        recorder.BindResourceBindings(pipeline_handle_, binding_set);
        recorder.DrawIndexed();
    }

    void RenderScene::Cleanup()
    {
        pipeline_handle_ = {};
        texture_handle_ = {};
        sampler_handle_ = {};
        mesh_handle_ = {};
    }
}
