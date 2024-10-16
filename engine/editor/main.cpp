#include "editor/include/editor.h"
#include "runtime/engine.h"
// Main code
int main(int argc, char** argv)
{
    kpengine::editor::Editor editor;
    kpengine::runtime::Engine* engine = new kpengine::runtime::Engine();
    editor.Initialize(engine);
    editor.Run();
    editor.Clear();
    delete engine;
    return 0;
}
