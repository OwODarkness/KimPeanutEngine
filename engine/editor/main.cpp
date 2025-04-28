#include <memory> 
#include <iostream>
#include "editor/include/editor.h"
#include "runtime/engine.h"


int main(int argc, char** argv)
{
    using Engine = kpengine::runtime::Engine;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    
    engine->Initialize();
    engine->Run();
    engine->Clear();
    return 0;
}
