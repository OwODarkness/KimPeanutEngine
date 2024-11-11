#include "render_object.h"

#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "runtime/core/system/render_system.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/render_mesh.h"
#include "runtime/runtime_global_context.h"
#include "runtime/render/render_camera.h"

namespace kpengine
{
    RenderObject::RenderObject(std::vector<std::shared_ptr<RenderMesh>> meshes, const std::string vertex_shader_path, const std::string fragment_shader_path) : meshes_(meshes),
                                                                                                                                                                shader_helper_(std::make_shared<RenderShader>(vertex_shader_path, fragment_shader_path))
    {
    }

    void RenderObject::Initialize()
    {
        shader_helper_->Initialize();

        for (std::shared_ptr<RenderMesh> &mesh : meshes_)
        {
            mesh->Initialize(shader_helper_);
        }
    }

    void RenderObject::SetLocation(const glm::vec3 &location)
    {
        location_ = location;
    }
    void RenderObject::SetScale(const glm::vec3 &scale)
    {
        scale_ = scale;
    }

    void RenderObject::Render()
    {
        shader_helper_->UseProgram();
        glm::mat4 transform = glm::mat4(1);
        transform = glm::scale(transform, scale_);
        transform = glm::translate(transform, location_);
        shader_helper_->SetMat("model", glm::value_ptr(transform));

        std::shared_ptr<RenderCamera> render_camera = runtime::global_runtime_context.render_system_->GetRenderCamera();
        glm::mat4 view_transform = render_camera->GetViewMatrix();
        glm::mat4 projection_transform = render_camera->GetProjectionMatrix();
        shader_helper_->SetMat("view", glm::value_ptr(view_transform));
        shader_helper_->SetMat("projection", glm::value_ptr(projection_transform));

        for (std::shared_ptr<RenderMesh> &mesh : meshes_)
        {
            mesh->Draw();
        }
    }
}