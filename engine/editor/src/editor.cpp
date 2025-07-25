#include "editor.h"
#include <cassert>

#include "editor/include/editor_ui.h"
#include "editor/include/editor_global_context.h"
#include "editor/include/editor_scene_manager.h"
#include "editor/include/editor_log_manager.h"

#include "runtime/runtime_global_context.h"
#include "runtime/core/system/window_system.h"
#include "runtime/core/system/render_system.h"
#include "runtime/engine.h"
#include "runtime/core/log/logger.h"


namespace kpengine::editor
{

        Editor::Editor():
        editor_ui(std::make_shared<kpengine::ui::EditorUI>()),
        is_initialized_(false)
        {
        }

        Editor::~Editor()
        {
        }

        void Editor::Initialize(kpengine::runtime::Engine* engine)
        {
            engine_ = engine;

            assert(engine_);
            assert(editor_ui);

             editor::EditorContextInitInfo global_editor_context_init_info{
                runtime::global_runtime_context.window_system_.get(),
                runtime::global_runtime_context.render_system_.get(),
                runtime::global_runtime_context.log_system_.get(),
                runtime::global_runtime_context.level_system_.get(),
                runtime::global_runtime_context.input_system_.get(),
                engine};
            editor::global_editor_context.editor = this;
            editor::global_editor_context.Initialize(global_editor_context_init_info);
            
            editor_ui->Initialize(global_editor_context.window_system_->GetOpenGLWndow());

            KP_LOG("EditorLog", LOG_LEVEL_DISPLAY, "Editor Initialize Successfully");
            is_initialized_ = true;
        }

        void Editor::Tick()
        {
            if(is_initialized_ == false)
            {
                return ;
            }
            editor_ui->BeginDraw();
            global_editor_context.editor_scene_manager_->Tick();
            global_editor_context.editor_log_manager_->Tick();
            editor_ui->Render();
            editor_ui->EndDraw();
        }

        void Editor::Clear()
        {
            editor_ui->Close();
        }

}