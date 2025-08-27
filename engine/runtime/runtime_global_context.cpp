#include "runtime_global_context.h"
#include "window/window_system.h"
#include "render/render_system.h"
#include "log/log_system.h"
#include "game_framework/world_system.h"
#include "input/input_system.h"

namespace kpengine
{
    namespace runtime{

       RuntimeContext global_runtime_context;

        RuntimeContext::RuntimeContext():
        window_system_(WindowSystem::CreateWindow(WindowAPIType::WINDOW_API_GLFW)),        
        render_system_(std::make_unique<RenderSystem>()),
        log_system_(std::make_unique<LogSystem>()),
        world_system_(std::make_unique<WorldSystem>()),
        input_system_(std::make_unique<input::InputSystem>()),
        graphics_api_type_(GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
        }
        void RuntimeContext::Initialize()
        {
            
            WindowCreateInfo window_create_info{};
            window_create_info.width = 1920;
            window_create_info.height = 1080;
            window_create_info.title = "KimPeanut Engine";
            window_create_info.graphics_api_type = GraphicsAPIType::GRAPHICS_API_OPENGL;
            window_system_->Initialize(window_create_info);
            
            input_system_->BindCursorEvent(window_system_->cursor_event_dispatcher_);
            input_system_->BindKeyEvent(window_system_->key_event_dispatcher_);
            input_system_->BindCursorEvent(window_system_->cursor_event_dispatcher_);
            input_system_->BindScrollEvent(window_system_->scroll_event_dispatcher_);
            input_system_->Initialize();


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
            log_system_.reset();
        }

        RuntimeContext::~RuntimeContext() = default;

    }
}