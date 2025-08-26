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
}