#include "render_scene.h"

#include <iostream>
#include <cassert>
#include <glad/glad.h>

#include "runtime/runtime_global_context.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/shader_pool.h"
#include "runtime/render/render_texture.h"
#include "runtime/render/frame_buffer.h"
#include "runtime/test/render_object_test.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/render_camera.h"
#include "runtime/render/shadow_maker.h"
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

namespace kpengine
{

    enum class RenderSceneMode : uint8_t
    {
        SceneMode_Editor
    };

    RenderScene::RenderScene() : scene_fb_(std::make_shared<FrameBuffer>(1280, 720)),
                                 render_camera_(nullptr),
                                 directional_shadow_maker_(std::make_shared<DirectionalShadowMaker>()),
                                 point_shadow_maker_(std::make_shared<PointShadowMaker>()),
                                 light_(Light()),
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
        glBufferData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_camera_matrices_); // Bind to binding point 0

        glGenBuffers(1, &ubo_light_);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo_light_);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(Light), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo_light_); // Bind to binding point 1

        directional_shadow_maker_->Initialize();

        point_shadow_maker_->Initialize();
        light_.point_light.ambient = {1.f, 1.f, 1.f};
        light_.point_light.position = {4.f, 5.f, 0.f};
        light_.directional_light.color = {0.f, 0.f, 0.f};
        light_.spot_light.color = {0.f, 0.f, 0.f};
        light_.spot_light.cutoff = std::cos(math::DegreeToRadian(12.5f));
        light_.spot_light.outer_cutoff = std::cos(math::DegreeToRadian(17.5f));
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
            proxy->visible_this_frame = frustum.Contains(world_AABB);
        }

        // render a depth map
        directional_shadow_maker_->BindFrameBuffer();
        Vector3f light_pos = -light_.directional_light.direction * 2.f;
        std::vector<Matrix4f> shadow_transforms;
        directional_shadow_maker_->CalculateShadowTransform(light_pos, shadow_transforms);
        Matrix4f light_space_matrix = shadow_transforms[0].Transpose();
        std::shared_ptr<RenderShader> depth_shader = directional_shadow_maker_->GetShader();
        depth_shader->UseProgram();
        depth_shader->SetMat("light_space_matrix", light_space_matrix[0]);
        RenderContext depth_shader_context = {.shader = depth_shader};
        for (auto &proxy : scene_proxies)
        {
            if (!proxy->visible_this_frame)
            {
                continue;
            }
            proxy->Draw(depth_shader_context);
        }

        directional_shadow_maker_->UnBindFrameBuffer();

        shadow_transforms.clear();

        // render a point depth map
        point_shadow_maker_->BindFrameBuffer();
        point_shadow_maker_->CalculateShadowTransform(light_.point_light.position, shadow_transforms);
        std::shared_ptr<RenderShader> point_depth_shader = point_shadow_maker_->GetShader();
        point_depth_shader->UseProgram();
        for (int i = 0; i < shadow_transforms.size(); i++)
        {
            point_depth_shader->SetMat(("shadow_matrices[" + std::to_string(i) + ']').c_str(), shadow_transforms[i].Transpose()[0]);
        }
        point_depth_shader->SetVec3("light_position", light_.point_light.position.Data());
        point_depth_shader->SetFloat("far_plane", 25.f);
        RenderContext point_shader_context{.shader = point_depth_shader};
        for (auto &proxy : scene_proxies)
        {
            if (!proxy->visible_this_frame)
            {
                continue;
            }
            proxy->Draw(point_shader_context);
        }
        point_shadow_maker_->UnBindFrameBuffer();
        // GBuffer update
        RenderContext geo_pass_context{
            .shader = geometry_shader_,
            .near_plane = render_camera_->z_near_,
            .far_plane = render_camera_->z_far_
        };
        g_buffer_->BindFrameBuffer();

        for (auto &proxy : scene_proxies)
        {
            if (!proxy->visible_this_frame)
            {
                continue;
            }
            proxy->DrawGeometryPass(geo_pass_context);
        }

        g_buffer_->UnBindFrameBuffer();

        scene_fb_->BindFrameBuffer();

        glBindBuffer(GL_UNIFORM_BUFFER, ubo_camera_matrices_);

        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Matrix4f), proj_mat.Transpose()[0]);
        glBufferSubData(GL_UNIFORM_BUFFER, sizeof(Matrix4f), sizeof(Matrix4f), view_mat.Transpose()[0]);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        if (is_light_dirty)
        {
            glBindBuffer(GL_UNIFORM_BUFFER, ubo_light_);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Light), &light_);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
            is_light_dirty = false;
        }
        // render skybox
        Vector3f cam_pos = render_camera_->GetPosition();

        RenderContext lighting_pass_context = {
            .shader = light_pass_shader_,
            .near_plane = render_camera_->z_near_,
            .far_plane = render_camera_->z_far_,
            .view_position = cam_pos.Data(),
            .directional_shadow_map = directional_shadow_maker_->GetShadowMap(),
            .point_shadow_map = point_shadow_maker_->GetShadowMap(),
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
        skybox->Render();

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        postprocess_pipeline_->Execute({.g_object_id = g_buffer_->GetTexture("g_object_id"),
                                        .texel_size = {1.f / 1280.f, 1.f / 720.f}});
        glDisable(GL_BLEND);

        // forward render
        //  for (auto &proxy : scene_proxies)
        //  {
        //      if (!proxy->visible_this_frame)
        //      {
        //          continue;
        //      }
        //      proxy->UpdateLightSpace(light_space_matrix[0]);
        //      proxy->Draw(lighting_pass_context);
        //  }

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

        context.shader->SetVec3("view_position", context.view_position);
        context.shader->SetFloat("near_plane", context.near_plane);
        context.shader->SetFloat("far_plane", context.far_plane);

        context.shader->SetInt("shadow_map", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, context.directional_shadow_map);

        context.shader->SetInt("point_shadow_map", 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, context.point_shadow_map);

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

        glBindVertexArray(fullscreen_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        glActiveTexture(GL_TEXTURE0);
    }
}