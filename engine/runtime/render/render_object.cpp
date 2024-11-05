#include "render_object.h"

#include "runtime/render/render_shader.h"
#include "runtime/render/render_mesh.h"


namespace kpengine
{
    RenderObject::RenderObject(RenderMesh* mesh,const std::string vertex_shader_path,const std::string fragment_shader_path):
    mesh_(mesh),
    shader_helper_(std::make_shared<RenderShader>(vertex_shader_path, fragment_shader_path))
    {
    }



    void RenderObject::Initialize()
    {
        shader_helper_->Initialize();
        mesh_->Initialize(shader_helper_);
    }

    void RenderObject::Render()
    {
        shader_helper_->UseProgram();
        mesh_->Draw();

        
    }
}