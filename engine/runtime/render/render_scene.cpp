#include "render_scene.h"

#include <iostream>
#include <cassert>
#include <glad/glad.h>

#include "runtime/runtime_global_context.h"
#include "runtime/core/system/render_system.h"
#include "runtime/render/shader_pool.h"

#include "runtime/render/frame_buffer.h"
#include "runtime/test/render_object_test.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/render_camera.h"
#include "runtime/render/shadow_maker.h"

#include "runtime/render/render_mesh_resource.h"
#include "platform/path/path.h"
#include "runtime/render/primitive_scene_proxy.h"
#include "runtime/render/skybox.h"
namespace kpengine
{

    enum class RenderSceneMode : uint8_t
    {
        SceneMode_Editor
    };

    RenderScene::RenderScene():
    scene_(std::make_shared<FrameBuffer>(1280, 720)),
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

        scene_->Initialize();
        
        //skybox
       skybox = test::GetRenderObjectSkybox();
        
       skybox->Initialize();


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
        // render a depth map
        directional_shadow_maker_->BindFrameBuffer();
        Vector3f light_pos = -light_.directional_light.direction * 2.f;
        std::vector<Matrix4f> shadow_transforms; 
        directional_shadow_maker_->CalculateShadowTransform(light_pos, shadow_transforms);
        Matrix4f light_space_matrix = shadow_transforms[0].Transpose();
        std::shared_ptr<RenderShader> depth_shader = directional_shadow_maker_->GetShader();
        depth_shader->UseProgram();
        depth_shader->SetMat("light_space_matrix", light_space_matrix[0]);

        for(auto& proxy: scene_proxies)
        {
            proxy->Draw(depth_shader);
        }

        directional_shadow_maker_->UnBindFrameBuffer();

        shadow_transforms.clear();

        //render a point depth map
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

        for(auto& proxy: scene_proxies)
        {
            Matrix4f transform_mat = Matrix4f::MakeTransformMatrix(proxy->GetTransform()).Transpose();
            point_depth_shader->SetMat("model", transform_mat[0]);
            proxy->Draw(point_depth_shader);
        }

        point_shadow_maker_->UnBindFrameBuffer();

        // render a normal scene
        scene_->BindFrameBuffer();
        {
            glBindBuffer(GL_UNIFORM_BUFFER, ubo_camera_matrices_);
            Matrix4f proj_mat = render_camera_->GetProjectionMatrix().Transpose();
            Matrix4f view_mat = render_camera_->GetViewMatrix().Transpose();
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Matrix4f), proj_mat[0]);
            glBufferSubData(GL_UNIFORM_BUFFER, sizeof(Matrix4f), sizeof(Matrix4f), view_mat[0]);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);

            if(is_light_dirty)
            {   
                glBindBuffer(GL_UNIFORM_BUFFER, ubo_light_);
                glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(Light), &light_);
                glBindBuffer(GL_UNIFORM_BUFFER, 0);
                is_light_dirty = false;
            }

            //render skybox
            skybox->Render();

            glActiveTexture(GL_TEXTURE15);
            glBindTexture(GL_TEXTURE_2D, directional_shadow_maker_->GetShadowMap());
            glActiveTexture(GL_TEXTURE14);
            glBindTexture(GL_TEXTURE_CUBE_MAP, point_shadow_maker_->GetShadowMap());

            for(auto& proxy: scene_proxies)
            {
                Vector3f cam_pos = render_camera_->GetPosition();
                proxy->UpdateViewPosition(cam_pos.Data());
                proxy->UpdateLightSpace(light_space_matrix[0]);
                proxy->Draw(current_shader);
            }
        }
        scene_->UnBindFrameBuffer();
    }

    void RenderScene::SetCurrentShader(const std::shared_ptr<RenderShader>& shader)
    {
        current_shader = shader;
    }


    SceneProxyHandle RenderScene::AddProxy(std::shared_ptr<PrimitiveSceneProxy> scene_proxy)
    {
        if(scene_proxy == nullptr)
        {
            return SceneProxyHandle::InValid();
        }
        //if free_slot exist, consume it
        if(free_slots.size())
        {
            unsigned int id = *(free_slots.end() - 1);
            free_slots.erase(free_slots.end() - 1);
            scene_proxies[id] = scene_proxy;
            return SceneProxyHandle(id, current_generation);
        }
        else
        {
            unsigned int id = current_generation;
            SceneProxyHandle handle(id, id);
            if(handle.IsValid())
            {
                ++current_generation;
                scene_proxies.push_back(scene_proxy);
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
        if(current_generation < handle_index)
        {
            return ;
        }
        else
        {
            scene_proxies[handle_index] = nullptr;
            free_slots.push_back(handle_index);
        }
    }
}