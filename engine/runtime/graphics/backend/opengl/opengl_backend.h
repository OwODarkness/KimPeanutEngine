#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_BACKEND_H

#include <memory>
#include <unordered_map>
#include <glad/glad.h>

#include "common/render_backend.h"
#include "opengl_context.h"


namespace kpengine::graphics
{   
     struct UniformBufferObject
    {
        alignas(16) Matrix4f model;
        alignas(16) Matrix4f view;
        alignas(16) Matrix4f proj;
    };


    class OpenglBackend : public RenderBackend
    {
    public:
        OpenglBackend();
    public:
        virtual void Initialize() override;
        virtual void BeginFrame() override;
        virtual void EndFrame() override;
        virtual void Present() override;
        virtual void Cleanup() override;
    public:
        BufferHandle CreateVertexBuffer(const void* data, size_t size) override;
        BufferHandle CreateIndexBuffer(const void* data, size_t size) override;
        bool DestroyBufferResource(BufferHandle handle) override;

    private:
        void CreatePipeline();
        void CreateMeshes();
        void CreateUniformBuffers();
        void UpdateUniformBuffers();
    private:
        std::unique_ptr<class MeshManager> mesh_manager_;
        std::unique_ptr<class TextureManager> texture_manager_;
        std::unique_ptr<class SamplerManager> sampler_manager_;
        std::unique_ptr<class ShaderManager> shader_manager_;
        std::unique_ptr<class OpenglPipeline> pipeline_;
        std::unique_ptr<class IModelLoader> model_loader_;


        std::unordered_map<MeshHandle, uint32_t> meshes_;
        OpenglContext context_;

        GLuint camera_ubo_;

    };
}

#endif