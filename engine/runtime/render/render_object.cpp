#include "render_object.h"

#include "runtime/render/render_shader.h"
#include "runtime/render/render_mesh.h"


namespace kpengine
{
    RenderObject::RenderObject(std::vector<std::shared_ptr<RenderMesh>> meshes,const std::string vertex_shader_path,const std::string fragment_shader_path):
    meshes_(meshes),
    shader_helper_(std::make_shared<RenderShader>(vertex_shader_path, fragment_shader_path))
    {
    }



    void RenderObject::Initialize()
    {
        shader_helper_->Initialize();
        for(std::shared_ptr<RenderMesh> & mesh : meshes_)
        {
            mesh->Initialize(shader_helper_);
        }
    }

    void RenderObject::Render()
    {
        shader_helper_->UseProgram();
        for(std::shared_ptr<RenderMesh> & mesh : meshes_)
        {
            mesh->Draw();
        }
    }
}