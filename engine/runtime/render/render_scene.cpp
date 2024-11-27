#include "render_scene.h"

#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "runtime/render/frame_buffer.h"
#include "runtime/render/render_object.h"
#include "runtime/test/render_object_test.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/render_camera.h"
#include "runtime/render/shadow_maker.h"

namespace kpengine
{

    enum class RenderSceneMode : uint8_t
    {
        SceneMode_Editor
    };

    void RenderScene::Initialize(std::shared_ptr<RenderCamera> camera)
    {
        scene_ = std::make_shared<FrameBuffer>(1280, 720);
        scene_->Initialize();

       
        skybox = test::GetRenderObjectSkyBox();
        skybox->Initialize();

        render_objects_.push_back(test::GetRenderObjectFloor());
        std::shared_ptr<RenderObject> bunny = test::GetRenderObjectBunny();
        bunny->SetLocation(glm::vec3(0.f, -0.8f, 0.f));
        render_objects_.push_back(bunny);
        std::shared_ptr<RenderObject> teapot = test::GetRenderObjectTeapot();
        teapot->SetLocation(glm::vec3(1.f, -0.5f, 1.f));
        render_objects_.push_back(teapot);

        for (int i = 0; i < render_objects_.size(); i++)
        {
            render_objects_[i]->Initialize();
            unsigned int shader_id = render_objects_[i]->GetShader()->GetShaderProgram();
            unsigned int uniform_block_index = glGetUniformBlockIndex(shader_id, "Matrices");
            glUniformBlockBinding(shader_id, uniform_block_index, 0);
        }

        render_camera_ = camera;

        glGenBuffers(1, &ubo_matrices_);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo_matrices_);
        glBufferData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), nullptr, GL_STATIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        glBindBufferRange(GL_UNIFORM_BUFFER, 0, ubo_matrices_, 0, 2 * sizeof(glm::mat4));

        // directional_light_.color = glm::vec3(0.);
        spot_light_.color = glm::vec3(0.f);
        spot_light_.cutoff = (float)std::cos(glm::radians(12.5));
        spot_light_.outer_cutoff = (float)std::cos(glm::radians(17.5));

        point_light_.color = glm::vec3(0.f, 0.f, 0.f);

        shadow_maker_ = std::make_shared<ShadowMaker>(1024, 1024);
        shadow_maker_->Initialize();
    }

    void RenderScene::Render()
    {
        // render a depth map
        glCullFace(GL_FRONT);
        glViewport(0, 0, shadow_maker_->width_, shadow_maker_->height_);

        glBindFramebuffer(GL_FRAMEBUFFER, shadow_maker_->GetShadowBuffer());
        glClear(GL_DEPTH_BUFFER_BIT);

        glm::vec3 light_pos = -directional_light_.direction * 2.f;

        glm::mat4 light_projection, light_view;
        glm::mat4 light_space_matrix;
        float near_plane = 1.f, far_plane = 7.5f;

        light_projection = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, near_plane, far_plane);
        light_view = glm::lookAt(light_pos, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
        light_space_matrix = light_projection * light_view;

        std::shared_ptr<RenderShader> depth_shader = shadow_maker_->GetShader();
        depth_shader->UseProgram();
        depth_shader->SetMat("light_space_matrix", glm::value_ptr(light_space_matrix));

        for (int i = 0; i < render_objects_.size(); i++)
        {
            render_objects_[i]->Render(depth_shader);
        }
        glCullFace(GL_BACK);
        // render a normal scene
        BeginDraw();
        glBindBuffer(GL_UNIFORM_BUFFER, ubo_matrices_);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(render_camera_->GetProjectionMatrix()));
        glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(render_camera_->GetViewMatrix()));
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        skybox->GetShader()->UseProgram();
        skybox->Render(skybox->GetShader());

        for (int i = 0; i < render_objects_.size(); i++)
        {
            std::shared_ptr<RenderShader> shader = render_objects_[i]->GetShader();
            shader->UseProgram();
            shader->SetVec3("ambient", glm::value_ptr(ambient_light_.ambient));

            ConfigurePointLightInfo(shader);
            ConfigureDirectionalLightInfo(shader);
            ConfigureSpotLightInfo(shader);

            shader->SetVec3("view_position", glm::value_ptr(render_camera_->GetPosition()));
            shader->SetMat("light_space_matrix", glm::value_ptr(light_space_matrix));

            glActiveTexture(GL_TEXTURE15);
            shader->SetInt("shadow_map", 15);
            glBindTexture(GL_TEXTURE_2D, shadow_maker_->GetShadowMap());

            render_objects_[i]->Render(shader);
        }

        EndDraw();
    }

    void RenderScene::BeginDraw()
    {
        scene_->BindFrameBuffer();
    }

    void RenderScene::EndDraw()
    {
        scene_->UnBindFrameBuffer();
    }

    void RenderScene::ConfigurePointLightInfo(std::shared_ptr<RenderShader> shader)
    {
        shader->SetVec3("point_light.position", glm::value_ptr(point_light_.position));
        shader->SetVec3("point_light.color", glm::value_ptr(point_light_.color));
        shader->SetVec3("point_light.ambient", glm::value_ptr(point_light_.ambient));
        shader->SetVec3("point_light.diffuse", glm::value_ptr(point_light_.diffuse));
        shader->SetVec3("point_light.specular", glm::value_ptr(point_light_.specular));
        shader->SetFloat("point_light.constant", point_light_.constant);
        shader->SetFloat("point_light.linear", point_light_.linear);
        shader->SetFloat("point_light.quadratic", point_light_.quadratic);
    }

    void RenderScene::ConfigureSpotLightInfo(std::shared_ptr<RenderShader> shader)
    {
        spot_light_.position = render_camera_->GetPosition();
        spot_light_.direction = render_camera_->GetDirection();
        shader->SetVec3("spot_light.position", glm::value_ptr(spot_light_.position));
        shader->SetVec3("spot_light.direction", glm::value_ptr(spot_light_.direction));
        shader->SetVec3("spot_light.color", glm::value_ptr(spot_light_.color));
        shader->SetVec3("spot_light.ambient", glm::value_ptr(spot_light_.ambient));
        shader->SetVec3("spot_light.diffuse", glm::value_ptr(spot_light_.diffuse));
        shader->SetVec3("spot_light.specular", glm::value_ptr(spot_light_.specular));
        shader->SetFloat("spot_light.constant", spot_light_.constant);
        shader->SetFloat("spot_light.linear", spot_light_.linear);
        shader->SetFloat("spot_light.quadratic", spot_light_.quadratic);
        shader->SetFloat("spot_light.cutoff", spot_light_.cutoff);
        shader->SetFloat("spot_light.outer_cutoff", spot_light_.outer_cutoff);
    }

    void RenderScene::ConfigureDirectionalLightInfo(std::shared_ptr<RenderShader> shader)
    {

        shader->SetVec3("directional_light.direction", glm::value_ptr(directional_light_.direction));
        shader->SetVec3("directional_light.color", glm::value_ptr(directional_light_.color));
        shader->SetVec3("directional_light.ambient", glm::value_ptr(directional_light_.ambient));
        shader->SetVec3("directional_light.diffuse", glm::value_ptr(directional_light_.diffuse));
        shader->SetVec3("directional_light.specular", glm::value_ptr(directional_light_.specular));
    }
}