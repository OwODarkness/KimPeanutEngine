#include "runtime_global_context.h"
#include "runtime/core/system/window_system.h"
#include "runtime/core/system/render_system.h"
#include "runtime/core/system/log_system.h"
#include "runtime/core/system/asset_system.h"
#include "runtime/core/system/world_system.h"
#include "runtime/core/system/input_system.h"

namespace kpengine
{
    namespace runtime{

       RuntimeContext global_runtime_context;

        RuntimeContext::RuntimeContext():
        window_system_(std::make_unique<WindowSystem>()),        
        render_system_(std::make_unique<RenderSystem>()),
        log_system_(std::make_unique<LogSystem>()),
        asset_system_(std::make_unique<AssetSystem>()),
        world_system_(std::make_unique<WorldSystem>()),
        input_system_(std::make_unique<input::InputSystem>()),
        glfw_context_(std::make_shared<GLFWAppContext>()),
        graphics_backend_type_(GraphicsBackEndType::GRAPHICS_BACKEND_OPENGL)
        {
        }
        void RuntimeContext::Initialize()
        {
            glfw_context_->window_sys = window_system_.get();
            glfw_context_->input_sys = input_system_.get();
            
            window_system_->Initialize(WindowInitInfo::GetDefaultWindowInfo());
            GLFWwindow* window = window_system_->GetOpenGLWndow();
            glfwSetWindowUserPointer(window, glfw_context_.get());
            window_system_->MakeContext();
            input_system_->Initialize(window);
            asset_system_->Initialize();

            render_system_->Initialize();
            
            world_system_->Initialize();
        }

        void RuntimeContext::PostInitialize()
        {
            render_system_->PostInitialize();   
        }

        void RuntimeContext::Clear()
        {
            window_system_.reset();
            render_system_.reset();
            asset_system_.reset();
            log_system_.reset();
        }

        RuntimeContext::~RuntimeContext() = default;

    }
}