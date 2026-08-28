#include "runtime_global_context.h"
#include "window/window_system.h"
#include "platform/memory_stats_sampler.h"
#include "render/render_system.h"
#include "gameplay/factory/static_mesh_actor_factory.h"
#include "gameplay/world/gameplay_world.h"
#include "log/log_system.h"
#include "log/logger.h"
#include "input/input_system.h"
#include "script/lua/lua_vm.h"

namespace kpengine
{
    namespace runtime
    {

        RuntimeContext global_runtime_context;

        RuntimeContext::RuntimeContext() :
        window_system_(WindowSystem::CreateWindowSystem(WindowAPIType::WINDOW_API_GLFW)),
        render_system_(std::make_unique<render::RenderSystem>()),
        gameplay_world_(std::make_unique<gameplay::GameplayWorld>(
            render_system_->GetRenderableSourceSink())),
        log_system_(std::make_unique<LogSystem>()),
        input_system_(std::make_unique<input::InputSystem>()),
        lua_vm_(std::make_unique<script::lua::LuaVM>()),
        memory_sampler_(MemoryStatsSampler::CreateMemoryStatsSampler(PlatformType::PLATFORM_WINDOWS)),
        graphics_api_type_(GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            
        }

        void RuntimeContext::Initialize()
        {
            WindowCreateInfo window_create_info{};
            window_create_info.width = 1920;
            window_create_info.height = 1080;
            window_create_info.title = "KimPeanut Engine";

            window_create_info.graphics_api_type = graphics_api_type_;
            window_system_->Initialize(window_create_info);


            lua_vm_->Initialize();

            // Render owns the resource pipeline and drains the async load queue the
            // bootstrap preload fed (docs/async/async_resource_queue.md).
            render::RenderSystemInitInfo render_init_info{};
            render_init_info.api_type = graphics_api_type_;
            render_init_info.native_window = window_system_->GetNativeHandle();
            render_init_info.resize_dispatcher = &window_system_->resize_event_dispatcher_;
            render_init_info.load_queue = &async_load_queue_;
            render_init_info.bootstrap_scene = bootstrap_scene_;
            render_system_->Initialize(render_init_info);

            // [reconstruction] Old design — input/render/world were wired and initialized
            // here; reconstructed later. The bootstrap preload flow (docs/status.md item 6)
            // is the replacement entry point for the render side.
            // input_system_->BindCursorEvent(window_system_->cursor_event_dispatcher_);
            // input_system_->BindKeyEvent(window_system_->key_event_dispatcher_);
            // input_system_->BindCursorEvent(window_system_->cursor_event_dispatcher_);
            // input_system_->BindScrollEvent(window_system_->scroll_event_dispatcher_);
            // input_system_->Initialize();
            // render_system_->Initialize();
            // world_system_->Initialize();
        }

        void RuntimeContext::PostInitialize()
        {
            // Bootstrap pass: drain the queued requests (load + process) before the
            // main loop, so first-frame pipeline requests are cache hits.
            render_system_->PostInitialize();
            bootstrap_renderable_source_ = render_system_->TakeBootstrapRenderableSource();
        }

        void RuntimeContext::FinalizeGameStartup()
        {
            if (!gameplay_world_ || !bootstrap_renderable_source_.has_value())
            {
                return;
            }

            const render::StaticMeshRenderableSourceDesc source =
                std::move(*bootstrap_renderable_source_);
            bootstrap_renderable_source_.reset();

            gameplay::StaticMeshActorDesc actor_desc{};
            actor_desc.mesh_asset = source.mesh_asset;
            actor_desc.material_asset = source.material_asset;
            actor_desc.transform = source.world_transform;
            actor_desc.local_bounds = source.world_bounds;
            actor_desc.visible = source.flags.visible;
            actor_desc.casts_shadow = source.flags.casts_shadow;
            actor_desc.lod_bias = source.lod_bias;
            if (!gameplay::CreateStaticMeshActor(*gameplay_world_, actor_desc).IsValid())
            {
                KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                       "Bootstrap gameplay actor could not be created");
            }
        }

        void RuntimeContext::Clear()
        {
            // This is called by the render thread after ImGui shuts down, while
            // the graphics context is still current. Release GPU objects before
            // the GLFW window/context they depend on.
            // Components enqueue source destruction through RenderSystem. The
            // gameplay World must therefore die before the sink and GPU teardown.
            gameplay_world_.reset();
            bootstrap_renderable_source_.reset();
            if (render_system_)
            {
                render_system_->Shutdown();
                render_system_.reset();
            }
            if (window_system_)
            {
                window_system_->Cleanup();
                window_system_.reset();
            }
            log_system_.reset();
            lua_vm_.reset();
            memory_sampler_.reset();
        }

        void RuntimeContext::SetBootstrapScene(render::BootstrapSceneInfo scene)
        {
            bootstrap_scene_ = std::move(scene);
        }

        RuntimeContext::~RuntimeContext() = default;

    }
}
