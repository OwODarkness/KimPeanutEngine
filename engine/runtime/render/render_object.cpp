#include "render_object.h"

#include <iostream>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "runtime/render/render_shader.h"
#include "runtime/render/render_mesh.h"

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
        // transform =
        transform = glm::translate(transform, location_);
        shader_helper_->SetMat("model", glm::value_ptr(transform));
        for (std::shared_ptr<RenderMesh> &mesh : meshes_)
        {
            mesh->Draw();
        }
    }
}