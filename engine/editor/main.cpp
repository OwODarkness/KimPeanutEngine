#include <memory> 
#include "editor/include/editor.h"
#include "runtime/engine.h"

#include "runtime/core/math/math_header.h"



int main(int argc, char** argv)
{

    using Engine = kpengine::runtime::Engine;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    
    engine->Initialize();
    engine->Run();
    engine->Clear();
    return 0;
}
