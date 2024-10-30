#include "editor.h"

#include <cassert>

#include "editor/include/editor_ui.h"
#include "editor/include/editor_global_context.h"
#include "runtime/runtime_global_context.h"
#include "runtime/core/system/window_system.h"
#include "runtime/engine.h"
#include "runtime/core/log/logger.h"


namespace kpengine
{
    namespace editor
    {

        Editor::Editor()
        {
        }

        Editor::~Editor()
        {
        }

        void Editor::Initialize(kpengine::runtime::Engine* engine)
        {
            engine_ = engine;

             editor::EditorContextInitInfo global_editor_context_init_info{runtime::global_runtime_context.window_system_.get()};

            editor::global_editor_context.Initialize(global_editor_context_init_info);

            editor_ui = std::make_shared<kpengine::ui::EditorUI>();
            editor_ui->Initialize(global_editor_context.window_system_->GetOpenGLWndow());


        }

        void Editor::Run()
        {
            assert(engine_);
            assert(editor_ui);
            while (true)
            {
                editor_ui->BeginDraw();
                {
                    editor_ui->Render();
                }
                editor_ui->EndDraw();

                if(!engine_->Tick())
                {
                    break;
                }
            }
            
        }

        void Editor::Clear()
        {
            editor_ui->Close();
        }

    }
}