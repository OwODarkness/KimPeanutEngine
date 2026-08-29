#include "render_scene.h"

#include <stdexcept>

#include "math/math_header.h"
#include "render_resource_resolver.h"

namespace kpengine::render
{
#define KP_RENDER_SCENE_LOG_NAME "RenderSceneLog"

    void RenderScene::Initialize(const RenderSceneInitInfo &info)
    {
        if (!info.resources.mesh.IsValid() || !info.resources.material_instance.IsValid())
        {
            throw std::runtime_error("RenderScene requires a mesh and material instance");
        }
        mesh_handle_ = info.resources.mesh;
        material_instance_ = info.resources.material_instance;
    }

    void RenderScene::Record(FrameContext &frame, graphics::CommandRecorder &recorder,
                             const MaterialSystem &materials,
                             const RenderResourceResolver &resource_resolver)
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

        const std::vector<graphics::ResourceBinding> draw_bindings{
            graphics::UniformBufferBinding{0, 0, per_pass.buffer, per_pass.offset, per_pass.range},
            graphics::UniformBufferBinding{0, 1, per_object.buffer, per_object.offset, per_object.range},
        };
        const FrameMaterialBinding material_binding = frame.CreateMaterialBinding(
            materials, resource_resolver, material_instance_, draw_bindings, MaterialPass::Scene);
        if (!frame.IsMaterialBindingCurrent(material_binding))
        {
            return;
        }

        recorder.BindPipeline(material_binding.pipeline);
        recorder.BindMesh(mesh_handle_);
        recorder.BindResourceBindings(material_binding.pipeline, material_binding.descriptor_set);
        recorder.DrawIndexed();
    }

    void RenderScene::Cleanup()
    {
        mesh_handle_ = {};
        material_instance_ = {};
    }
}
