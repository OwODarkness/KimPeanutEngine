#ifndef KPENGINE_RUNTIME_RENDER_OBJECT_H
#define KPENGINE_RUNTIME_RENDER_OBJECT_J

#include <string>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace kpengine
{

    struct Transformation
    {
        glm::vec3 location{0.f, 0.f, 0.f};
        glm::vec3 rotation{0.f, 0.f, 0.f};
        glm::vec3 scale{1.f, 1.f, 1.f};
    };

    class RenderMesh;
    class RenderShader;

    class RenderObject
    {
    public:
        RenderObject(std::vector<std::shared_ptr<RenderMesh>> meshes, const std::string vertex_shader_path, const std::string fragment_shader_path);
        RenderObject(std::vector<std::shared_ptr<RenderMesh>> meshes, std::shared_ptr<RenderShader> shader_helper);

        virtual void Initialize();
        //before render object, please use shader first
        virtual void Render(std::shared_ptr<RenderShader> shader);
        virtual void SetLocation(const glm::vec3 &location);
        virtual void SetScale(const glm::vec3 &scale);
        inline std::shared_ptr<RenderShader> GetShader() const { return shader_helper_; }

    protected:
        std::vector<std::shared_ptr<RenderMesh>> meshes_;
        std::shared_ptr<RenderShader> shader_helper_;
    };

    class RenderSingleObject : public RenderObject
    {
    public:
        RenderSingleObject(std::vector<std::shared_ptr<RenderMesh>> meshes, const std::string vertex_shader_path, const std::string fragment_shader_path);
        RenderSingleObject(std::vector<std::shared_ptr<RenderMesh>> meshes, std::shared_ptr<RenderShader> shader_helper);

        virtual void Initialize() override;
        virtual void Render(std::shared_ptr<RenderShader> shader) override;
        virtual void SetLocation(const glm::vec3 &location) override;
        virtual void SetScale(const glm::vec3 &scale) override;

    protected:
        Transformation transformation_;

    };

    class RenderMultipleObject: public RenderObject
    {
    public:
        virtual void Initialize() override;

        RenderMultipleObject(std::vector<std::shared_ptr<RenderMesh>> meshes, const std::string vertex_shader_path, const std::string fragment_shader_path);
        RenderMultipleObject(std::vector<std::shared_ptr<RenderMesh>> meshes, std::shared_ptr<RenderShader> shader_helper);
        virtual void Render(std::shared_ptr<RenderShader> shader) override;
        std::vector<Transformation> transformations_;
        std::vector<glm::mat4> model_matrices;
    };
}

#endif