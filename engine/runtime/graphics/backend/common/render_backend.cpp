#include "render_backend.h"
#if KPENGINE_GRAPHICS_ENABLE_OPENGL
#include "opengl/opengl_backend.h"
#endif
#if KPENGINE_GRAPHICS_ENABLE_VULKAN
#include "vulkan/vulkan_backend.h"
#endif
namespace kpengine::graphics{
    std::unique_ptr<RenderBackend> RenderBackend::CreateGraphicsBackEnd(GraphicsAPIType backend_type)
    {
        if(GraphicsAPIType::GRAPHICS_API_OPENGL == backend_type)
        {
#if KPENGINE_GRAPHICS_ENABLE_OPENGL
            return std::make_unique<OpenglBackend>();
#else
            return nullptr;
#endif
        }
        else if(GraphicsAPIType::GRAPHICS_API_VULKAN == backend_type)
        {
#if KPENGINE_GRAPHICS_ENABLE_VULKAN
            return std::make_unique<VulkanBackend>();
#else
            return nullptr;
#endif
        }
        return nullptr;
    }

    void RenderBackend::BindWindowResize(EventDispatcher<ResizeEvent>& dispatcher)
    {
        dispatcher.Bind(std::bind(&RenderBackend::FramebufferResizeCallback, this, std::placeholders::_1));
    }

    void RenderBackend::FramebufferResizeCallback(const ResizeEvent& event) 
    {
        width_ = event.width;
        height_ = event.height;
    }
    
}
