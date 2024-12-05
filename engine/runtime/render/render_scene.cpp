#include "render_scene.h"

#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

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

        directional_light_.color = glm::vec3(0.);
        spot_light_.color = glm::vec3(0.f);
        spot_light_.cutoff = (float)std::cos(glm::radians(12.5));
        spot_light_.outer_cutoff = (float)std::cos(glm::radians(17.5));

        //point_light_.color = glm::vec3(0.f, 0.f, 0.f);
        point_light_.position = glm::vec3(4.f, 2.f, 0.f);
        ambient_light_.ambient = glm::vec3(0.2f);

        directional_shadow_maker_ = std::make_shared<DirectionalShadowMaker>();
        directional_shadow_maker_->Initialize();
        point_shadow_maker_ = std::make_shared<PointShadowMaker>();
        point_shadow_maker_->Initialize();
    }

    void RenderScene::Render(float deltatime)
    {
        angle += 5.f * deltatime;
        point_light_.position.x = 4 * std::cos(glm::radians(angle));
        point_light_.position.z = 4* std::sin(glm::radians(angle));

        //render_objects_[1]->SetRotation({0.f,0.f, angle});

        // render a depth map
        directional_shadow_maker_->BindFrameBuffer();
        glm::vec3 light_pos = -directional_light_.direction * 2.f;
        std::vector<glm::mat4> shadow_transforms; 
        directional_shadow_maker_->CalculateShadowTransform(light_pos, shadow_transforms);
        glm::mat4 light_space_matrix = shadow_transforms[0];
        std::shared_ptr<RenderShader> depth_shader = directional_shadow_maker_->GetShader();
        depth_shader->UseProgram();
        depth_shader->SetMat("light_space_matrix", glm::value_ptr(light_space_matrix));

        for (int i = 0; i < render_objects_.size(); i++)
        {
            render_objects_[i]->Render(depth_shader);
        }
        directional_shadow_maker_->UnBindFrameBuffer();

        shadow_transforms.clear();

        //render a point depth map
        point_shadow_maker_->BindFrameBuffer();
        point_shadow_maker_->CalculateShadowTransform(point_light_.position, shadow_transforms);
        std::shared_ptr<RenderShader> point_depth_shader = point_shadow_maker_->GetShader();
        point_depth_shader->UseProgram();
        for (int i = 0; i < shadow_transforms.size(); i++)
        {
            point_depth_shader->SetMat(("shadow_matrices[" + std::to_string(i) + ']').c_str(), glm::value_ptr(shadow_transforms[i]));
        }
        point_depth_shader->SetVec3("light_position", glm::value_ptr(point_light_.position));
        point_depth_shader->SetFloat("far_plane", 25.f);
        for(int i = 0;i<render_objects_.size();i++)
        {
            point_depth_shader->SetMat("model", glm::value_ptr(render_objects_[i]->CalculateModelMatrix()));
            render_objects_[i]->Render(point_depth_shader);
        }
        point_shadow_maker_->UnBindFrameBuffer();

        // render a normal scene
        scene_->BindFrameBuffer();
        {
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
                glBindTexture(GL_TEXTURE_2D, directional_shadow_maker_->GetShadowMap());

                shader->SetFloat("far_plane", 25.f);
                glActiveTexture(GL_TEXTURE14);
                shader->SetInt("point_shadow_map", 14);
                glBindTexture(GL_TEXTURE_CUBE_MAP, point_shadow_maker_->GetShadowMap());

                render_objects_[i]->Render(shader);
            }
        }
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