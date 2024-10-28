#include "editor.h"

#include <cassert>

#include "editor/include/editor_ui.h"
#include "runtime/engine.h"
#include "runtime/core/log/logger.h"
#include "shader/shader.h"
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
            editor_ui = std::make_shared<kpengine::ui::EditorUI>();
            editor_ui->Initialize(kpengine::ui::WindowInitInfo::GetDefaultWindowInfo());

            kpengine::ShaderHelper helper("normal.vs", "normal.fs");
            helper.Initialize();
        }

        void Editor::Run()
        {
            assert(engine_);
            assert(editor_ui);
            while (true)
            {
                if(!editor_ui->Render())
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