#include "render_scene.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/type_ptr.hpp>

#include "runtime/render/frame_buffer.h"
#include "runtime/render/render_object.h"
#include "runtime/test/render_object_test.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/render_camera.h"

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
         
         render_objects_.push_back(test::GetRenderObjectFloor());
         render_objects_.push_back(test::GetRenderObjectCube());
         //render_objects_.push_back(test::GetRenderObjectSkyBox());
         //
        // render_objects_.push_back(test::GetRenderObjectBunny());

        
        // std::shared_ptr<RenderObject> teapot  = test::GetRenderObjectTeapot();
        // teapot->SetLocation({10.f, 5.f, 3.f});
        // render_objects_.push_back(teapot);
        
        //render_objects_.push_back(test::GetRenderObjectPlanet());
        //render_objects_.push_back(test::GetRenderObjectRock());

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
        spot_light_.cutoff = std::cos(glm::radians(12.5));
        spot_light_.outer_cutoff = std::cos(glm::radians(17.5));

        point_light_.color = glm::vec3(0.f, 0.f, 0.f);
    }

    void RenderScene::Render()
    {
        BeginDraw();

        for (int i = 0; i < render_objects_.size(); i++)
        {
            render_objects_[i]->Render();
            render_objects_[i]->GetShader()->SetVec3("ambient", glm::value_ptr(ambient_light_.ambient));

            render_objects_[i]->GetShader()->SetVec3("point_light.position", glm::value_ptr(point_light_.position));
            render_objects_[i]->GetShader()->SetVec3("point_light.color", glm::value_ptr(point_light_.color));
            render_objects_[i]->GetShader()->SetVec3("point_light.ambient", glm::value_ptr(point_light_.ambient));
            render_objects_[i]->GetShader()->SetVec3("point_light.diffuse", glm::value_ptr(point_light_.diffuse));
            render_objects_[i]->GetShader()->SetVec3("point_light.specular", glm::value_ptr(point_light_.specular));
            render_objects_[i]->GetShader()->SetFloat("point_light.constant", point_light_.constant);
            render_objects_[i]->GetShader()->SetFloat("point_light.linear", point_light_.linear);
            render_objects_[i]->GetShader()->SetFloat("point_light.quadratic", point_light_.quadratic);

            render_objects_[i]->GetShader()->SetVec3("directional_light.direction", glm::value_ptr(directional_light_.direction));
            render_objects_[i]->GetShader()->SetVec3("directional_light.color", glm::value_ptr(directional_light_.color));
            render_objects_[i]->GetShader()->SetVec3("directional_light.ambient", glm::value_ptr(directional_light_.ambient));
            render_objects_[i]->GetShader()->SetVec3("directional_light.diffuse", glm::value_ptr(directional_light_.diffuse));
            render_objects_[i]->GetShader()->SetVec3("directional_light.specular", glm::value_ptr(directional_light_.specular));


            spot_light_.position = render_camera_->GetPosition();
            spot_light_.direction = render_camera_->GetDirection();
            render_objects_[i]->GetShader()->SetVec3("spot_light.position", glm::value_ptr(spot_light_.position));
            render_objects_[i]->GetShader()->SetVec3("spot_light.direction", glm::value_ptr(spot_light_.direction));
            render_objects_[i]->GetShader()->SetVec3("spot_light.color", glm::value_ptr(spot_light_.color));
            render_objects_[i]->GetShader()->SetVec3("spot_light.ambient", glm::value_ptr(spot_light_.ambient));
            render_objects_[i]->GetShader()->SetVec3("spot_light.diffuse", glm::value_ptr(spot_light_.diffuse));
            render_objects_[i]->GetShader()->SetVec3("spot_light.specular", glm::value_ptr(spot_light_.specular));
            render_objects_[i]->GetShader()->SetFloat("spot_light.constant", spot_light_.constant);
            render_objects_[i]->GetShader()->SetFloat("spot_light.linear", spot_light_.linear);
            render_objects_[i]->GetShader()->SetFloat("spot_light.quadratic", spot_light_.quadratic);
            render_objects_[i]->GetShader()->SetFloat("spot_light.cutoff", spot_light_.cutoff);
            render_objects_[i]->GetShader()->SetFloat("spot_light.outer_cutoff", spot_light_.outer_cutoff);

            render_objects_[i]->GetShader()->SetVec3("view_position", glm::value_ptr(render_camera_->GetPosition()));

            glBindBuffer(GL_UNIFORM_BUFFER, ubo_matrices_);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(render_camera_->GetProjectionMatrix()));
            glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(render_camera_->GetViewMatrix()));

            glBindBuffer(GL_UNIFORM_BUFFER, 0);
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

}