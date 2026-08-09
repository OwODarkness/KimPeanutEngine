#include <memory>
#include <iostream>
#include "runtime/engine.h"

namespace kpengine::example
{
    void RenderExample()
    {
        using namespace runtime;
        try
        {
            std::unique_ptr<Engine> engine = std::make_unique<Engine>();
            engine->Initialize();
            engine->Run();
            engine->Clear();
        }
        catch (std::exception e)
        {
            std::cout << e.what() << std::endl;
        }
    }
}
