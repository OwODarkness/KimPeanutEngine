#include <memory>
#include <iostream>

#include "runtime/engine.h"
using namespace kpengine::runtime;
int main(int argc, char **argv)
{
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

    return 0;
}
