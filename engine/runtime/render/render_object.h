#ifndef KPENGINE_RUNTIME_RENDER_OBJECT_H
#define KPENGINE_RUNTIME_RENDER_OBJECT_J

#include <string>
#include <memory>
namespace kpengine{
    class RenderMesh;
    class RenderShader;

    class RenderObject{
    public:
         RenderObject(RenderMesh* mesh,const std::string vertex_shader_path,const std::string fragment_shader_path);
        void Initialize();
        void Render();
        inline  std::shared_ptr<RenderShader> GetShader() const {return shader_helper_;}
    private:
        std::shared_ptr<RenderMesh> mesh_;
        std::shared_ptr<RenderShader> shader_helper_;
    };
}

#endif