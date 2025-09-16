#include "opengl_backend.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "log/logger.h"
namespace kpengine::graphics{
    void OpenglBackend::Initialize()
    {
        if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            KP_LOG("OpenglBackendLog", LOG_LEVEL_ERROR, "Failed to load OpenGL Loader");
            std::runtime_error("Failed to load OpenGL Loader");
        }
    }
    void OpenglBackend::BeginFrame()
    {

    }
    void OpenglBackend::EndFrame()
    {
    }

    void OpenglBackend::Present()
    {
        
    }

    void OpenglBackend::Cleanup()
    {

    }

    BufferHandle OpenglBackend::CreateVertexBuffer(const void* data, size_t size)
    {
        GLuint vbo{};
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
        
        return {vbo, 0};
    }

    BufferHandle OpenglBackend::CreateIndexBuffer(const void* data, size_t size)
    {
        GLuint ebo{};
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);

        return {ebo, 0};
    }

    bool OpenglBackend::DestroyBufferResource(BufferHandle handle)
    {
        glDeleteBuffers(1, &handle.id);
        return true;
    }
}