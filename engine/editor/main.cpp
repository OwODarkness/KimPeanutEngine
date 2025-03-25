#include <memory> 
#include "editor/include/editor.h"
#include "runtime/engine.h"

#include "runtime/core/system/delegate.h"
#include <iostream>
DECLARE_DELEGATE(FOnNotify)


int main(int argc, char** argv)
{
    using Engine = kpengine::runtime::Engine;
    using Editor = kpengine::editor::Editor;

    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    engine->Initialize();

    std::unique_ptr<Editor> editor = std::make_unique<Editor>();
    editor->Initialize(engine.get());
    
    editor->Run();
    editor->Clear();


}
