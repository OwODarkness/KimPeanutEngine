#include "render_backend.h"
#include "opengl/opengl_backend.h"
#include "vulkan/vulkan_backend.h"
namespace kpengine::graphics{
    std::unique_ptr<RenderBackend> RenderBackend::CreateGraphicsBackEnd(GraphicsAPIType backend_type)
    {
        if(GraphicsAPIType::GRAPHICS_API_OPENGL == backend_type)
        {
            return std::make_unique<OpenglBackend>();
        }
        else if(GraphicsAPIType::GRAPHICS_API_VULKAN == backend_type)
        {
            return std::make_unique<VulkanBackend>();
        }
        return nullptr;
    }
    
}