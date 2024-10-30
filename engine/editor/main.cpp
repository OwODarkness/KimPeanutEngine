#include "editor/include/editor.h"
#include "runtime/engine.h"
#include <iostream>
// Main code
int main(int argc, char** argv)
{
    kpengine::runtime::Engine* engine = new kpengine::runtime::Engine();
    engine->Initialize();

    kpengine::editor::Editor* editor = new kpengine::editor::Editor();
    editor->Initialize(engine);
    
    editor->Run();

    editor->Clear();
    delete engine;
    delete editor;
    
    return 0;
}
