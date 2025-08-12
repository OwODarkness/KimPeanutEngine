#include "render_scene.h"

#include <iostream>
#include <cassert>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "runtime/runtime_global_context.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/shader_pool.h"
#include "runtime/render/render_texture.h"
#include "runtime/render/frame_buffer.h"
#include "runtime/test/render_object_test.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/render_camera.h"
#include "runtime/render/environment_map.h"
#include "runtime/render/render_mesh_resource.h"
#include "platform/path/path.h"
#include "runtime/render/primitive_scene_proxy.h"
#include "runtime/render/skybox.h"
#include "runtime/render/aabb.h"
#include "runtime/render/frustum.h"
#include "runtime/render/gizmos.h"
#include "runtime/render/postprocess_pipeline.h"
#include "runtime/render/outline_effect.h"
#include "runtime/render/shadow_manager.h"
#include "runtime/render/render_light.h"

namespace kpengine
{

    enum class RenderSceneMode : uint8_t
    {
        SceneMode_Editor
    };

    RenderScene::RenderScene() : scene_fb_(std::make_shared<FrameBuffer>(1280, 720)),
                                 render_camera_(nullptr),
                                 current_shader(nullptr)
    {
    }

    void RenderScene::Initialize(std::shared_ptr<RenderCamera> camera)
    {
        assert(camera);
        render_camera_ = camera;

        scene_fb_->Initialize();
        scene_fb_->AddColorAttachment("default", GL_RGB, GL_RGB, GL_UNSIGNED_INT);
        scene_fb_->Finalize();

        // environment map
        environment_map_wrapper = std::make_shared<EnvironmentMapWrapper>("texture/hdr/venice_sunset_1k.hdr");
        environment_map_wrapper->Initialize();

        // skybox
        // skybox = test::GetRenderObjectSkybox();
        skybox = std::make_shared<Skybox>(runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_SKYBOX), environment_map_wrapper->GetEnvironmentMap());
        skybox->Initialize();

        g_buffer_ = std::make_shared<FrameBuffer>(1280, 720);
        g_buffer_->Initialize();
        g_buffer_->AddColorAttachment("g_position", GL_RGB16F, GL_RGB, GL_FLOAT);
        g_buffer_->AddColorAttachment("g_normal", GL_RGB16F, GL_RGB, GL_FLOAT);
        g_buffer_->AddColorAttachment("g_albedo", GL_RGB8, GL_RGB, GL_FLOAT);
        g_buffer_->AddColorAttachment("g_material", GL_RGB8, GL_RGB, GL_FLOAT);
        g_buffer_->AddColorAttachment("g_object_id", GL_RGB16F, GL_RGB, GL_FLOAT);
        g_buffer_->AddColorAttachment("g_depth", GL_RGB16F, GL_RGB, GL_FLOAT);
        g_buffer_->Finalize();

        geometry_shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_NORMAL);

        InitFullScreenTriangle();
        light_pass_shader_ = runtime::global_runtime_context.render_system_->GetShaderPool()->GetShader(SHADER_CATEGORY_DEFER_PBR);

        postprocess_pipeline_ = std::make_shared<PostProcessPipeline>();
        postprocess_pipeline_->AddEffect(std::make_shared<OutlineEffect>());
        postprocess_pipeline_->Initialize();

        glGenBuffers(1, &ubo_camera_matrices_);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo_camera_matrices_);
        glBufferData(GL_UNIFORM_BUFFER, 2 * sizeof(Matrix4f), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_camera_matrices_); // Bind to binding point 0
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        glGenBuffers(1, &light_ssbo_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, light_ssbo_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GPULightData) * KPENGINE_MAX_LIGHTS, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, light_ssbo_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        shadow_manager_ = std::make_unique<ShadowManager>();

        std::shared_ptr<DirectionalLightData> light0 = std::make_shared<DirectionalLightData>();
        lights_.push_back(light0);
        light0->intensity = 0.2f;
        shadow_manager_->AddLight(light0);

        std::shared_ptr<PointLightData> light1 = std::make_shared<PointLightData>();
        lights_.push_back(light1);
        light1->position = {0.f, 2.f, 0.f};
        shadow_manager_->AddLight(light1);


        shadow_manager_->Initialize();
    }

    void RenderScene::Render(float deltatime)
    {
        Matrix4f proj_mat = render_camera_->GetProjectionMatrix();
        Matrix4f view_mat = render_camera_->GetViewMatrix();
        Frustum frustum = ExtractFrustumFromVPMat(proj_mat * view_mat);

        // mark visiable
        for (auto &proxy : scene_proxies)
        {
            Matrix4f transform_mat = Matrix4f::MakeTransformMatrix(proxy->GetTransform());
            AABB world_AABB = GetWorldAABB(proxy->GetAABB(), transform_mat);
            proxy->SetVisibility(frustum.Contains(world_AABB));
        }

        shadow_manager_->Render(scene_proxies);

        // GBuffer update
        RenderContext geo_pass_context{
            .shader = geometry_shader_,
            .near_plane = render_camera_->z_near_,
            .far_plane = render_camera_->z_far_};
        g_buffer_->BindFrameBuffer();

        for (auto &proxy : scene_proxies)
        {
            if (!proxy->IsVisible())
            {
                continue;
            }
            proxy->DrawGeometryPass(geo_pass_context);
        }

        g_buffer_->UnBindFrameBuffer();

        debug_id = shadow_manager_->GetDirectionalShadowMap();
        scene_fb_->BindFrameBuffer();

        glBindBuffer(GL_UNIFORM_BUFFER, ubo_camera_matrices_);

        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Matrix4f), proj_mat.Transpose()[0]);
        glBufferSubData(GL_UNIFORM_BUFFER, sizeof(Matrix4f), sizeof(Matrix4f), view_mat.Transpose()[0]);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        // ssbo
        int current_light_count = lights_.size();

        if (current_light_count < KPENGINE_MAX_LIGHTS)
        {
            std::vector<GPULightData> light_data;

            for (int i = 0; i < current_light_count; i++)
            {
                light_data.push_back(lights_[i]->ToGPULightData());
            }

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, light_ssbo_);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GPULightData) * current_light_count, light_data.data());
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }

        // render skybox
        Vector3f cam_pos = render_camera_->GetPosition();

        RenderContext lighting_pass_context = {
            .shader = light_pass_shader_,
            .light_num = current_light_count,
            .near_plane = render_camera_->z_near_,
            .far_plane = render_camera_->z_far_,
            .view_position = cam_pos.Data(),
            .light_space_matrix = shadow_manager_->GetLightSpaceMatrix().Transpose()[0],
            .directional_shadow_map = shadow_manager_->GetDirectionalShadowMap(),
            .point_shadow_map = shadow_manager_->GetPointShadowMap(),
            .irradiance_map = environment_map_wrapper->GetIrradianceMap()->GetTexture(),
            .prefilter_map = environment_map_wrapper->GetPrefilterMap()->GetTexture(),
            .brdf_map = environment_map_wrapper->GetBRDFMap()->GetTexture(),
            .g_position = g_buffer_->GetTexture("g_position"),
            .g_normal = g_buffer_->GetTexture("g_normal"),
            .g_albedo = g_buffer_->GetTexture("g_albedo"),
            .g_material = g_buffer_->GetTexture("g_material")};

        ExecLightingRenderPass(lighting_pass_context);

        glEnable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_buffer_->GetFBO());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scene_fb_->GetFBO());
        glBlitFramebuffer(
            0, 0, 1280, 720, // src rect
            0, 0, 1280, 720, // dst rect
            GL_DEPTH_BUFFER_BIT,
            GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, scene_fb_->GetFBO());
        if (is_skybox_visible)
        {
            skybox->Render();
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        postprocess_pipeline_->Execute({.g_object_id = g_buffer_->GetTexture("g_object_id"),
                                        .texel_size = {1.f / 1280.f, 1.f / 720.f}});
        glDisable(GL_BLEND);

        if (gizmos_)
        {
            gizmos_->Draw();
        }

        scene_fb_->UnBindFrameBuffer();
    }

    void RenderScene::SetCurrentShader(const std::shared_ptr<RenderShader> &shader)
    {
        current_shader = shader;
    }

    void RenderScene::SetRenderAxis(std::shared_ptr<Gizmos> gizmos)
    {
        gizmos_ = gizmos;
    }

    SceneProxyHandle RenderScene::AddProxy(std::shared_ptr<PrimitiveSceneProxy> scene_proxy)
    {
        if (scene_proxy == nullptr)
        {
            return SceneProxyHandle::InValid();
        }
        // if free_slot exist, consume it
        if (free_slots.size())
        {
            unsigned int id = *(free_slots.end() - 1);
            free_slots.erase(free_slots.end() - 1);
            scene_proxies[id] = scene_proxy;
            return SceneProxyHandle(id, current_generation);
        }
        else
        {
            unsigned int id = current_generation;
            SceneProxyHandle handle(id, current_generation);
            if (handle.IsValid())
            {
                ++current_generation;
                scene_proxies.push_back(scene_proxy);
                scene_proxy->id_ = handle.id;
                return handle;
            }
            else
            {
                return SceneProxyHandle::InValid();
            }
        }
    }

    void RenderScene::RemoveProxy(SceneProxyHandle handle)
    {
        unsigned int handle_index = handle.id;
        if (current_generation < handle_index)
        {
            return;
        }
        else
        {
            scene_proxies[handle_index] = nullptr;
            free_slots.push_back(handle_index);
        }
    }

    void RenderScene::InitFullScreenTriangle()
    {
        float triangleVertices[] = {
            //  x     y
            -1.0f, -1.0f,
            3.0f, -1.0f,
            -1.0f, 3.0f};

        glGenVertexArrays(1, &fullscreen_vao);
        glGenBuffers(1, &fullscreen_vbo);

        glBindVertexArray(fullscreen_vao);

        glBindBuffer(GL_ARRAY_BUFFER, fullscreen_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(triangleVertices), triangleVertices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);

        glBindVertexArray(0);
    }

    void RenderScene::ExecLightingRenderPass(const RenderContext &context)
    {
        if (!context.shader)
            return;

        context.shader->UseProgram();
        context.shader->SetInt("light_num", context.light_num);
        context.shader->SetVec3("view_position", context.view_position);
        context.shader->SetMat("light_space_matrix", context.light_space_matrix);

        context.shader->SetFloat("near_plane", context.near_plane);
        context.shader->SetFloat("far_plane", context.far_plane);

        context.shader->SetInt("directional_shadow_map", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, context.directional_shadow_map);

        context.shader->SetInt("irradiance_map", 2);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_CUBE_MAP, context.irradiance_map);

        context.shader->SetInt("prefilter_map", 3);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_CUBE_MAP, context.prefilter_map);

        context.shader->SetInt("brdf_map", 4);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, context.brdf_map);

        context.shader->SetInt("g_position", 5);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, context.g_position);

        context.shader->SetInt("g_normal", 6);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, context.g_normal);

        context.shader->SetInt("g_albedo", 7);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, context.g_albedo);

        context.shader->SetInt("g_material", 8);
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, context.g_material);

        for (int i = 0; i < 4; i++)
        {
            context.shader->SetInt("point_shadow_map[" + std::to_string(i) + "]", 9 + i);
            glActiveTexture(GL_TEXTURE9 + i);
            glBindTexture(GL_TEXTURE_CUBE_MAP, context.point_shadow_map[i]);
        }

        glBindVertexArray(fullscreen_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        glActiveTexture(GL_TEXTURE0);
    }

    RenderScene::~RenderScene() = default;

}