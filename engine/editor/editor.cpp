#include "editor/editor.h"

#include <cassert>

#include "editor/ui/editor_ui.h"
#include "editor/context/editor_context.h"
#include "runtime/runtime_global_context.h"
#include "runtime/window/window_system.h"
#include "log/logger.h"

namespace kpengine::editor
{

    Editor::Editor() : editor_ui_(std::make_shared<EditorUI>())
    {
    }

    Editor::~Editor() = default;

    void Editor::Initialize(kpengine::runtime::Engine *engine)
    {
        assert(engine);
        engine_ = engine;

        // Editor systems reach the runtime through the editor context, mirroring the
        // runtime's own global context. Runs on the main thread; nothing here touches
        // the GPU — the UI is built later on the render thread (InitEditorUI).
        runtime::RuntimeContext &runtime_ctx = runtime::global_runtime_context;
        EditorContextInitInfo init_info{};
        init_info.window_system = runtime_ctx.window_system_.get();
        init_info.render_system = runtime_ctx.render_system_.get();
        init_info.log_system = runtime_ctx.log_system_.get();
        init_info.input_system = runtime_ctx.input_system_.get();
        init_info.runtime_engine = engine;
        init_info.memory_sampler = runtime_ctx.memory_sampler_.get();
        init_info.graphics_api_type = runtime_ctx.graphics_api_type_;

        global_editor_context.editor = this;
        global_editor_context.Initialize(init_info);

        initialized_ = true;
        KP_LOG("LogEditor", LOG_LEVEL_INFO, "Editor initializing...");
    }

    void Editor::InitEditorUI()
    {
        assert(initialized_);
        assert(global_editor_context.window_system_);

        // Runs on the render thread — ImGui + GL/Vulkan context affinity requires it.
        EditorUIInitInfo init_info{};
        init_info.window = global_editor_context.window_system_->GetNativeHandle();
        init_info.editor_presentation_bridge =
            global_editor_context.render_system_->GetEditorPresentationBridge();
        init_info.log_system = global_editor_context.log_system_;
        init_info.engine = engine_;
        init_info.memory_sampler = global_editor_context.memory_sampler_;
        init_info.render_system = global_editor_context.render_system_;
        init_info.command_registry = runtime::global_runtime_context.GetCommandRegistry();
        init_info.input_system = global_editor_context.input_system_;
        init_info.window_system = global_editor_context.window_system_;
        init_info.camera_control_sink = &runtime::global_runtime_context;
        editor_ui_->Initialize(init_info);
    }

    void Editor::Tick()
    {
        if (!initialized_)
        {
            return;
        }
        // Render thread.
        editor_ui_->BeginDraw();
        editor_ui_->Render();
        editor_ui_->EndDraw();
    }

    void Editor::Clear()
    {
        // Called on the main thread after the render thread joined; the ImGui state
        // was already torn down there (CloseUI), this only resets editor state.
        global_editor_context.Clear();
        editor_ui_.reset();
        initialized_ = false;
    }

    void Editor::CloseUI()
    {
        if (editor_ui_)
        {
            editor_ui_->Close();
        }
    }

}
