#include "render_object.h"

#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "runtime/core/system/render_system.h"
#include "runtime/render/render_shader.h"
#include "runtime/render/render_mesh.h"
#include "runtime/runtime_global_context.h"

namespace kpengine
{
    RenderObject::RenderObject(std::vector<std::shared_ptr<RenderMesh>> meshes, const std::string vertex_shader_path, const std::string fragment_shader_path) : meshes_(meshes), shader_helper_(std::make_shared<RenderShader>(vertex_shader_path, fragment_shader_path))
    {
    }

    RenderObject::RenderObject(std::vector<std::shared_ptr<RenderMesh>> meshes, std::shared_ptr<RenderShader> shader_helper) : meshes_(meshes), shader_helper_(shader_helper)
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
    }
    void RenderObject::SetScale(const glm::vec3 &scale)
    {
    }

    void RenderObject::Render()
    {
    }

    RenderSingleObject::RenderSingleObject(std::vector<std::shared_ptr<RenderMesh>> meshes, const std::string vertex_shader_path, const std::string fragment_shader_path) : RenderObject(meshes, vertex_shader_path, fragment_shader_path)
    {
    }
    RenderSingleObject::RenderSingleObject(std::vector<std::shared_ptr<RenderMesh>> meshes, std::shared_ptr<RenderShader> shader_helper) : RenderObject(meshes, shader_helper)
    {
    }

    void RenderSingleObject::SetLocation(const glm::vec3 &location)
    {
        transformation_.location = location;
    }
    void RenderSingleObject::SetScale(const glm::vec3 &scale)
    {
        transformation_.scale = scale;
    }

    void RenderSingleObject::Render()
    {
        shader_helper_->UseProgram();
        glm::mat4 transform = glm::mat4(1);
        transform = glm::scale(transform, transformation_.scale);
        transform = glm::translate(transform, transformation_.location);
        shader_helper_->SetMat("model", glm::value_ptr(transform));

        for (std::shared_ptr<RenderMesh> &mesh : meshes_)
        {
            mesh->Draw();
        }
    }

    RenderMultipleObject::RenderMultipleObject(std::vector<std::shared_ptr<RenderMesh>> meshes, const std::string vertex_shader_path, const std::string fragment_shader_path) : RenderObject(meshes, vertex_shader_path, fragment_shader_path)
    {
    }
    RenderMultipleObject::RenderMultipleObject(std::vector<std::shared_ptr<RenderMesh>> meshes, std::shared_ptr<RenderShader> shader_helper) : RenderObject(meshes, shader_helper)
    {
    }

    void RenderMultipleObject::Initialize()
    {
        RenderObject::Initialize();
        model_matrices.reserve(transformations_.size());
        for (int i = 0; i < transformations_.size(); i++)
        {
            glm::mat4 transform = glm::mat4(1);
            transform = glm::scale(transform, transformations_[i].scale);

            transform = glm::rotate(transform, transformations_[i].rotation.x, glm::vec3(1.f, 0.f, 0.f));
            transform = glm::rotate(transform, transformations_[i].rotation.y, glm::vec3(0.f, 0.f, 1.f));
            transform = glm::rotate(transform, transformations_[i].rotation.z, glm::vec3(0.f, 1.f, 0.f));
            transform = glm::translate(transform, transformations_[i].location);

            model_matrices.push_back(transform);
        }
        unsigned int buffer;
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, transformations_.size() * sizeof(glm::mat4), &model_matrices[0], GL_STATIC_DRAW);

        for (int i = 0; i < meshes_.size(); i++)
        {
            glBindVertexArray(meshes_[i]->vao_);

            // location
            GLsizei vec4Size = sizeof(glm::vec4);
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * vec4Size, (void *)0);
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 4 * vec4Size, (void *)(vec4Size));
            glEnableVertexAttribArray(5);
            glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 4 * vec4Size, (void *)(2 * vec4Size));
            glEnableVertexAttribArray(6);
            glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, 4 * vec4Size, (void *)(3 * vec4Size));

            glVertexAttribDivisor(3, 1);
            glVertexAttribDivisor(4, 1);
            glVertexAttribDivisor(5, 1);
            glVertexAttribDivisor(6, 1);

            glBindVertexArray(0);
        }
        // location
    }

    void RenderMultipleObject::Render()
    {

        shader_helper_->UseProgram();
        for (std::shared_ptr<RenderMesh> &mesh : meshes_)
        {
            glBindVertexArray(mesh->vao_);
            glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)mesh->indices_.size(), GL_UNSIGNED_INT, 0, transformations_.size());
            glBindVertexArray(0);
            //mesh->Draw();
        }

        // for (int i = 0; i < transformations_.size(); i++)
        // {
        //     shader_helper_->UseProgram();
        //     glm::mat4 transform = glm::mat4(1);

        //     transform = glm::scale(transform, transformations_[i].scale);
        //     transform = glm::rotate(transform, transformations_[i].rotation.x, glm::vec3(1.f, 0.f, 0.f));
        //     transform = glm::rotate(transform, transformations_[i].rotation.y, glm::vec3(0.f, 0.f, 1.f));
        //     transform = glm::rotate(transform, transformations_[i].rotation.z, glm::vec3(0.f, 1.f, 0.f));

        //     transform = glm::translate(transform, transformations_[i].location);

        //     shader_helper_->SetMat("model", glm::value_ptr(transform));

        //     for (std::shared_ptr<RenderMesh> &mesh : meshes_)
        //     {
        //         mesh->Draw();
        //     }
        // }
    }

}