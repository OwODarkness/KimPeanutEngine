#ifndef KPENGINE_RUNTIME_RENDER_OBJECT_H
#define KPENGINE_RUNTIME_RENDER_OBJECT_J

#include <string>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace kpengine{
    class RenderMesh;
    class RenderShader;

    class RenderObject{
    public:
         RenderObject(std::vector<std::shared_ptr<RenderMesh>> meshes,const std::string vertex_shader_path,const std::string fragment_shader_path);
        void Initialize();
        void Render();
        void SetLocation(const glm::vec3& location);
        void SetScale(const glm::vec3& scale);
        inline  std::shared_ptr<RenderShader> GetShader() const {return shader_helper_;}
    private:
        std::vector<std::shared_ptr<RenderMesh>> meshes_;
        std::shared_ptr<RenderShader> shader_helper_;

        glm::vec3 location_{0.f, 0.f, 0.f};
        glm::vec3 rotation_{0.f, 0.f, 0.f};
        glm::vec3 scale_{1.f, 1.f, 1.f};
    };
}

#endif