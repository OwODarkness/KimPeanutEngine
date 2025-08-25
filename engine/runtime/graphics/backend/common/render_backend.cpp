#include "render_backend.h"
#include "opengl/opengl_backend.h"
#include "vulkan/vulkan_backend.h"
namespace kpengine::graphics{
    std::unique_ptr<RenderBackend> RenderBackend::CreateGraphicsBackEnd(GraphicsBackEndType backend_type)
    {
        if(GraphicsBackEndType::GRAPHICS_BACKEND_OPENGL == backend_type)
        {
            return std::make_unique<OpenglBackend>();
        }
        else if(GraphicsBackEndType::GRAPHICS_BACKEND_VULKAN == backend_type)
        {
            return std::make_unique<VulkanBackend>();
        }
        return nullptr;
    }
    
}