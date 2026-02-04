#include <memory>
#include <iostream>

#include "runtime/engine.h"
#include "runtime/window/glfw_window_system.h"
#include "runtime/input/input_system.h"
#include "runtime/input/input_context.h"
#include "runtime/game_framework/camera.h"
#include "runtime/graphics/backend/vulkan/vulkan_backend.h"
using namespace kpengine::runtime;
using namespace kpengine;

void renderer_test()
{
    try
    {
        std::unique_ptr<Engine> engine = std::make_unique<Engine>();
        engine->Initialize();
        engine->Run();
        engine->Clear();
    }
    catch (std::exception e)
    {
        std::cout << e.what() << std::endl;
    }
}

void rhi_test()
{
try{
        std::unique_ptr<WindowSystem> window = WindowSystem::CreateWindow(WindowAPIType::WINDOW_API_GLFW);
        WindowCreateInfo window_create_info;
        window_create_info.graphics_api_type = GraphicsAPIType::GRAPHICS_API_VULKAN;
        //window_create_info.graphics_api_type = GraphicsAPIType::GRAPHICS_API_OPENGL;
        window_create_info.width = 1920;
        window_create_info.height = 1080;
        window_create_info.title = "RHI";
        window->Initialize(window_create_info);

        std::unique_ptr<input::InputSystem> input = std::make_unique<input::InputSystem>();
        input->BindKeyEvent(window->key_event_dispatcher_);
        input->BindCursorEvent(window->cursor_event_dispatcher_);
        input->BindScrollEvent(window->scroll_event_dispatcher_);
        std::shared_ptr<input::InputContext> context = std::make_shared<input::InputContext>();
        input->AddContext("SceneInputContext", context);
        input->SetActiveContext("SceneInputContext");
        std::unique_ptr<Camera> camera = std::make_unique<Camera>();
        camera->Initialize(context.get());

        std::unique_ptr<graphics::RenderBackend> rhi = graphics::RenderBackend::CreateGraphicsBackEnd(window_create_info.graphics_api_type);
        rhi->BindWindowResize(window->resize_event_dispatcher_);
        rhi->window_ = static_cast<GLFWwindow*>(window->GetNativeHandle());
        rhi->Initialize();
        while(!window->ShouldClose())
        {
            if(window_create_info.graphics_api_type == GraphicsAPIType::GRAPHICS_API_OPENGL)
            {
                window->SwapBuffers();
            }
            window->PollEvents();
            CameraData camera_data = camera->GetCameraData();
            rhi->camera_data.view = camera_data.view;
            rhi->camera_data.proj = camera_data.proj;
            rhi->BeginFrame();
            rhi->EndFrame();
        }
        rhi->Cleanup();
        window->Cleanup();
    }
    catch (std::exception e)
    {
        std::cout << e.what() << std::endl;
    }
}

int main(int argc, char **argv)
{
    renderer_test();
    return 0;
}
