#ifndef KPENGINE_RUNTIME_RENDER_OBJECT_H
#define KPENGINE_RUNTIME_RENDER_OBJECT_J

#include <string>

namespace kpengine{
    class RenderMesh;
    class RenderShader;
    
    class RenderObject{
    public:
         RenderObject(RenderMesh* mesh,const std::string vertex_shader_path,const std::string fragment_shader_path);
        ~RenderObject();
        void Initialize();
        void Render();
        inline RenderShader* GetShader() const {return shader_helper_;}
    private:
        RenderMesh* mesh_;
        RenderShader* shader_helper_;
    
    };
}

#endif