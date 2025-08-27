#include <memory>
#include <iostream>

#include "runtime/engine.h"
#include "runtime/window/glfw_window_system.h"
#include "runtime/graphics/backend/vulkan/vulkan_backend.h"
using namespace kpengine::runtime;
using namespace kpengine;
int main(int argc, char **argv)
{
    // try
    // {
    //     std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    //     engine->Initialize();
    //     engine->Run();
    //     engine->Clear();
    // }
    // catch (std::exception e)
    // {
    //     std::cout << e.what() << std::endl;
    // }

    try{
        std::unique_ptr<WindowSystem> window = WindowSystem::CreateWindow(WindowAPIType::WINDOW_API_GLFW);

        WindowCreateInfo window_create_info;
        window_create_info.graphics_api_type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        window_create_info.width = 1920;
        window_create_info.height = 1080;
        window_create_info.title = "test";
        window->Initialize(window_create_info);
        std::unique_ptr<graphics::RenderBackend> rhi = graphics::RenderBackend::CreateGraphicsBackEnd(window_create_info.graphics_api_type);
        rhi->window_ = static_cast<GLFWwindow*>(window->GetNativeHandle());
        rhi->Initialize();
        while(!window->ShouldClose())
        {
            window->PollEvents();
        }
        rhi->Cleanup();
    }
    catch (std::exception e)
    {
        std::cout << e.what() << std::endl;
    }
    return 0;
}
